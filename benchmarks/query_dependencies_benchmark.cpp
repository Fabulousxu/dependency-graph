#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>
#include "json_serialization.hpp"
#include "package_loader.hpp"
#include "util.hpp"
#include "xpgraph.hpp"

struct Option {
  std::size_t sample_size;
  std::size_t max_depth;
  std::filesystem::path repository_config;
  std::filesystem::path cache_directory;
  std::filesystem::path temp_directory;
  std::filesystem::path output;
};

Option parse_args(int argc, char **argv) {
  Option option;
  CLI::App app("XPGraph query dependencies benchmark");
  app.add_option("--sample-size", option.sample_size, "Number of sampled packages to benchmark")
     ->required()->check(CLI::PositiveNumber);
  app.add_option("--max-depth", option.max_depth, "Maximum dependency query depth")
     ->required()->check(CLI::PositiveNumber);
  app.add_option("--repository-config", option.repository_config, "Repository config file to load")
     ->required()->check(CLI::ExistingFile);
  app.add_option("--cache-directory", option.cache_directory, "Repository cache directory to load")
     ->required()->check(CLI::ExistingDirectory);
  app.add_option("--temp-directory", option.temp_directory, "Temporary data directory")->default_val("temp");
  app.add_option("--output", option.output, "Benchmark result JSON path")
     ->default_val("query_dependencies_benchmark_report.json");
  try { app.parse(argc, argv); } catch (const CLI::ParseError &exc) { std::exit(app.exit(exc)); }
  return option;
}

std::vector<std::string> generate_samples(const xpg::XPGraph &graph, std::size_t sample_size) {
  std::vector<std::string> samples;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<std::size_t> dist(0, graph.buffer_package_count() - 1);
  while (samples.size() < sample_size) {
    auto pview = graph.get_package_in_buffer(dist(gen));
    if (pview.versions().size() < 2) continue;
    samples.emplace_back(pview.name);
  }
  return samples;
}

template <class Json>
double analyze_times(std::vector<std::size_t> &times, std::size_t sample_size, Json &j) {
  std::ranges::sort(times);
  auto total = std::accumulate(times.begin(), times.end(), 0ull);
  j["avg"] = total / sample_size / 1000.0;
  j["min"] = times.front() / 1000.0;
  j["max"] = times.back() / 1000.0;
  j["p50"] = times[sample_size / 2] / 1000.0;
  j["p75"] = times[sample_size * 3 / 4] / 1000.0;
  j["p90"] = times[sample_size * 9 / 10] / 1000.0;
  j["p95"] = times[sample_size * 19 / 20] / 1000.0;
  j["p99"] = times[sample_size * 99 / 100] / 1000.0;
  return total / sample_size / 1000.0;
}

template <class Json>
void run_benchmark(const xpg::XPGraph &graph, const std::vector<std::string> samples, std::size_t max_depth,
                   bool use_buffer, bool use_gpu, Json &j, std::string_view key = "") {
  auto run_query = [&, use_buffer, use_gpu](std::string_view name, std::size_t depth, bool tree) {
    if (use_buffer) graph.query_dependencies_in_buffer(name, "", "", depth, tree);
    else graph.query_dependencies(name, "", "", depth, tree, use_gpu);
  };
  Json jtree, jflat;
  std::vector<std::vector<std::size_t>> tree_times(max_depth), flat_times(max_depth);
  for (auto depth : std::views::iota(1ull, max_depth + 1)) {
    if (!key.empty()) xpg::println("[{}] Testing with depth={}...", key, depth);
    else xpg::println("Testing with depth={}...", depth);
    for (auto name : samples) {
      auto tree_time = xpg::measure_time<std::chrono::microseconds>([=] { run_query(name, depth, true); });
      tree_times[depth - 1].emplace_back(tree_time.count());
    }
    xpg::println("  Tree queries completed. Average {:.3f} ms per query",
                 analyze_times(tree_times[depth - 1], samples.size(), jtree["depth " + std::to_string(depth)]));
    for (auto name : samples) {
      auto flat_time = xpg::measure_time<std::chrono::microseconds>([=] { run_query(name, depth, false); });
      flat_times[depth - 1].emplace_back(flat_time.count());
    }
    xpg::println("  Flat queries completed. Average {:.3f} ms per query",
                 analyze_times(flat_times[depth - 1], samples.size(), jflat["depth " + std::to_string(depth)]));
  }
  j["tree_query"] = std::move(jtree);
  j["flat_query"] = std::move(jflat);
}

int main(int argc, char *argv[]) {
  auto [sample_size, max_depth, repository_config, cache_directory, temp_directory, output] = parse_args(argc, argv);
  std::ifstream file(repository_config);
  if (!file.good())
    throw std::runtime_error(std::format("Failed to open repository config file: {}.", repository_config.string()));
  auto config = nlohmann::json::parse(file).get<xpg::RepositoryConfig>();
  xpg::XPGraph graph(temp_directory, xpg::open_mode::kCreate);
  xpg::PackageLoader loader(graph);
  nlohmann::ordered_json jreport, jcpu, jgpu, jmem;
  auto load_time = xpg::measure_time<std::chrono::milliseconds>(
    [&] { loader.load_repositories(repository_config, cache_directory, false, true); });
  auto samples = generate_samples(graph, sample_size);
  xpg::println("=== XPGraph Query Dependencies Benchmark ===");
  xpg::println("Testing {} sample packages with depth from 1 to {}...", sample_size, max_depth);
  run_benchmark(graph, samples, max_depth, true, false, jmem, "uncompacted");
  auto flush_time = xpg::measure_time<std::chrono::milliseconds>([&] { loader.flush_buffer(false, true); });
  run_benchmark(graph, samples, max_depth, false, false, jcpu, "cpu");
  auto cache_time = xpg::measure_time<std::chrono::milliseconds>([&] { loader.build_cache(); });
  run_benchmark(graph, samples, max_depth, false, true, jgpu, "gpu");
  xpg::println("All tests completed.");
  xpg::println("==================================================");
  std::ranges::sort(samples);

  jreport["title"] = "XPGraph Query Dependencies Benchmark";
  jreport["time"] = xpg::now_iso8601();
  jreport["repositories"] = config;
  auto &jdata = jreport["data"];
  jdata["package_count"] = graph.package_count();
  jdata["version_count"] = graph.version_count();
  jdata["dependency_count"] = graph.dependency_count();
  jdata["load_seconds"] = load_time.count() / 1000.0;
  jdata["flush_seconds"] = flush_time.count() / 1000.0;
  jdata["cache_seconds"] = cache_time.count() / 1000.0;
  jreport["sample_size"] = sample_size;
  jreport["samples"] = std::move(samples);
  jreport["max_depth"] = max_depth;
  jreport["filter_architecture"] = true;
  jreport["filter_version"] = true;
  jreport["expand_alternative"] = true;
  jreport["latency_milliseconds"] =
    {{"cpu", {std::move(jcpu)}}, {"gpu", {std::move(jgpu)}}, {"uncompacted", {std::move(jmem)}}};
  if (!output.empty()) {
    auto report = jreport.dump(2);
    try {
      std::filesystem::create_directories(output.parent_path());
      std::ofstream(output) << report;
    } catch (const std::exception &e) {
      xpg::println("{}", report);
      xpg::println(std::cerr, "Failed to generate benchmark report: {}", e.what());
    }
    xpg::println("Generated benchmark report: {}", output.string());
  }
  xpg::println("Cleaning up...");
  graph.close();
  std::filesystem::remove_all(temp_directory);
  return 0;
}
