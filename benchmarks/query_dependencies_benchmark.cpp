#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <numeric>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>
#include "json_serialization.hpp"
#include "mgxpgraph.hpp"
#include "utils.hpp"
#include "xpgraph.hpp"

namespace xpg {

class QueryDependenciesBenchmark {
  using json_t = nlohmann::ordered_json;

public:
  int run(int argc, char *argv[]) {
    parse_args(argc, argv);
    init_graph();
    generate_samples();
    run_benchmark();
    generate_report();
    return 0;
  }

private:
  XPGraph xpgraph_;
  MGXPGraph mgxpgraph_;
  std::vector<std::string> samples_;
  std::size_t sample_size_ = 0;
  std::size_t max_depth_ = 0;
  std::filesystem::path data_directory_;
  std::filesystem::path repository_config_;
  std::filesystem::path output_;
  bool test_memgraph_ = false;
  std::string host_ = "127.0.0.1";
  std::uint16_t port_ = 0;
  json_t cpu_report_;
  json_t gpu_report_;
  json_t memgraph_report_;
  json_t memgraph_query_module_report_;

  void parse_args(int argc, char *argv[]) {
    CLI::App app("XPGraph query dependencies benchmark");
    app.add_option("--sample-size", sample_size_, "Number of sampled packages to benchmark")
       ->required()->check(CLI::PositiveNumber);
    app.add_option("--max-depth", max_depth_, "Maximum dependency query depth")
       ->required()->check(CLI::PositiveNumber);
    app.add_option("--data-directory", data_directory_, "XPGraph data directory")
       ->required()->check(CLI::ExistingDirectory);
    app.add_option("--repository-config", repository_config_, "Loaded repository config file")
       ->required()->check(CLI::ExistingFile);
    app.add_option("--output", output_, "Benchmark result JSON path")
       ->default_val("query_dependencies_benchmark_report.json");
    auto *test_memgraph =
      app.add_flag("--test-memgraph", test_memgraph_, "Benchmark against an existing Memgraph instance");
    app.add_option("--host", host_, "Memgraph host")->default_val("127.0.0.1");
    test_memgraph->needs(app.add_option("--port", port_, "Memgraph Bolt port")->check(CLI::Range(1, 65535)));
    try { app.parse(argc, argv); } catch (const CLI::ParseError &error) { std::exit(app.exit(error)); }
  }

  void init_graph() {
    println("Initializing...");
    xpgraph_.load(data_directory_);
    xpgraph_.build_cache();
    if (test_memgraph_) {
      mgxpgraph_.connect(host_, port_);
      mgxpgraph_.load_query_module();
    }
  }

  void generate_samples() {
    println("Generating {} random sample packages...", sample_size_);
    samples_.clear();
    samples_.reserve(sample_size_);
    std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<std::size_t> distribution(0, xpgraph_.package_count() - 1);
    while (samples_.size() < sample_size_) {
      auto package = xpgraph_.get_package(distribution(generator));
      if (package.versions().size() < 2) continue;
      if (std::ranges::find(samples_, package.name) != samples_.end()) continue;
      samples_.emplace_back(package.name);
    }
  }

  void run_benchmark() {
    println("=== XPGraph Query Dependencies Benchmark ===");
    println("Testing {} sample packages with depth from 1 to {}...", sample_size_, max_depth_);
    run_queries(xpgraph_, false, "cpu", cpu_report_);
    run_queries(xpgraph_, true, "gpu", gpu_report_);
    if (test_memgraph_) {
      run_queries(mgxpgraph_, false, "memgraph", memgraph_report_);
      run_queries(mgxpgraph_, true, "memgraph_query_module", memgraph_query_module_report_);
    }
    println("All tests completed.");
    println("==================================================");
  }

  void generate_report() {
    println("Generating report...");
    json_t report, repositories;
    try {
      std::ifstream config_file(repository_config_);
      if (!config_file.good())
        throw std::runtime_error(std::format(
          "Failed to open repository config file: {}.", repository_config_.string()));
      repositories = nlohmann::json::parse(config_file).get<RepositoryConfig>();
    } catch (const std::exception &error) { println(std::cerr, "{}", error.what()); }
    std::ranges::sort(samples_);
    report["title"] = "XPGraph Query Dependencies Benchmark";
    report["time"] = now_iso8601();
    report["repositories"] = repositories;
    auto &data = report["data"];
    data["package_count"] = xpgraph_.package_count();
    data["version_count"] = xpgraph_.version_count();
    data["dependency_count"] = xpgraph_.dependency_count();
    report["sample_size"] = sample_size_;
    report["samples"] = samples_;
    report["max_depth"] = max_depth_;
    report["satisfy_architecture"] = true;
    report["satisfy_version"] = true;
    report["expand_alternative"] = true;
    auto &latency = report["latency_milliseconds"];
    latency["cpu"] = std::move(cpu_report_);
    latency["gpu"] = std::move(gpu_report_);
    if (test_memgraph_) {
      latency["memgraph"] = std::move(memgraph_report_);
      latency["memgraph_query_module"] = std::move(memgraph_query_module_report_);
    }
    auto serialized = report.dump(2);
    if (output_.empty()) {
      println("{}", serialized);
      return;
    }
    try {
      auto parent = output_.parent_path();
      if (!parent.empty()) std::filesystem::create_directories(parent);
      std::ofstream(output_) << serialized;
      println("Generated benchmark report: {}", output_.string());
    } catch (const std::exception &error) {
      println("{}", serialized);
      println(std::cerr, "Failed to generate benchmark report: {}", error.what());
    }
  }

  static double analyze_times(std::vector<std::size_t> &times, json_t &report) {
    std::ranges::sort(times);
    auto total = std::accumulate(times.begin(), times.end(), 0ull);
    report["avg"] = total / times.size() / 1000.0;
    report["min"] = times.front() / 1000.0;
    report["max"] = times.back() / 1000.0;
    report["p50"] = times[times.size() / 2] / 1000.0;
    report["p75"] = times[times.size() * 3 / 4] / 1000.0;
    report["p90"] = times[times.size() * 9 / 10] / 1000.0;
    report["p95"] = times[times.size() * 19 / 20] / 1000.0;
    report["p99"] = times[times.size() * 99 / 100] / 1000.0;
    return total / times.size() / 1000.0;
  }

  static std::chrono::microseconds run_query(GraphBase &graph, std::string_view name, std::size_t depth, bool tree,
                                             bool use_accelerator) {
    std::chrono::microseconds elapsed;
    if (auto *mgxpgraph = dynamic_cast<MGXPGraph *>(&graph); mgxpgraph && use_accelerator)
      elapsed = measure_time<std::chrono::microseconds>([&, depth] {
        auto str = mgxpgraph->query_dependencies_use_query_modules(name, "", "", depth, tree, true, true, true);
      });
    else
      elapsed = measure_time<std::chrono::microseconds>([&, depth] {
        // nlohmann::ordered_json json = graph.query_dependencies(name, "", "", depth, tree, use_accelerator);
        // auto str = json.dump();
          graph.query_dependencies(name, "", "", depth, tree, use_accelerator);
      });
    if (auto *mgxpgraph = dynamic_cast<MGXPGraph *>(&graph)) mgxpgraph->clear_arena();
    return elapsed;
  }

  void run_queries(GraphBase &graph, bool use_accelerator, std::string_view key, json_t &report) const {
    json_t tree_report, flat_report;
    std::vector<std::vector<std::size_t>> tree_times(max_depth_), flat_times(max_depth_);
    for (auto depth : std::views::iota(1ull, max_depth_ + 1)) {
      println("[{}] Testing with depth={}...", key, depth);
      for (const auto &name : samples_)
        tree_times[depth - 1].emplace_back(run_query(graph, name, depth, true, use_accelerator).count());
      println("  Tree queries completed. Average {:.3f} ms per query",
              analyze_times(tree_times[depth - 1], tree_report["depth " + std::to_string(depth)]));
      for (const auto &name : samples_)
        flat_times[depth - 1].emplace_back(run_query(graph, name, depth, false, use_accelerator).count());
      println("  Flat queries completed. Average {:.3f} ms per query",
              analyze_times(flat_times[depth - 1], flat_report["depth " + std::to_string(depth)]));
    }
    report["tree_query"] = std::move(tree_report);
    report["flat_query"] = std::move(flat_report);
  }
};

} // namespace xpg

int main(int argc, char *argv[]) {
  try { return xpg::QueryDependenciesBenchmark().run(argc, argv); } catch (const std::exception &error) {
    xpg::println(std::cerr, "Benchmark failed: {}", error.what());
    return 1;
  }
}
