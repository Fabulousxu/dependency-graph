#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>
#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>
#include "dependency_graph.hpp"
#include "package_loader.hpp"
#include "util.hpp"

double analyze_times(nlohmann::ordered_json &result, std::vector<std::size_t> &times, std::size_t trials) {
  std::ranges::sort(times);
  auto total_time = std::accumulate(times.begin(), times.end(), 0ull);
  result["avg"] = std::format("{:.3f} ms", total_time / trials / 1000.0);
  result["min"] = std::format("{:.3f} ms", times.front() / 1000.0);
  result["max"] = std::format("{:.3f} ms", times.back() / 1000.0);
  result["p50"] = std::format("{:.3f} ms", times[trials / 2] / 1000.0);
  result["p75"] = std::format("{:.3f} ms", times[trials * 3 / 4] / 1000.0);
  result["p90"] = std::format("{:.3f} ms", times[trials * 9 / 10] / 1000.0);
  result["p95"] = std::format("{:.3f} ms", times[trials * 19 / 20] / 1000.0);
  result["p99"] = std::format("{:.3f} ms", times[trials * 99 / 100] / 1000.0);
  return total_time / trials / 1000.0;
}

struct Option {
  std::string dataset_file;
  std::size_t trials;
  std::size_t max_depth;
  std::size_t memory_limit;
  std::string output_file;
};

int main(int argc, char *argv[]) {
  Option opt;
  CLI::App app;
  app.add_option("--dataset", opt.dataset_file)->required()->check(CLI::ExistingFile);
  app.add_option("--trials", opt.trials)->required()->check(CLI::PositiveNumber);
  app.add_option("--max-depth", opt.max_depth)->required()->check(CLI::PositiveNumber);
  app.add_option("--memory-limit", opt.memory_limit)->required()->check(CLI::PositiveNumber);
  app.add_option("--output", opt.output_file)->default_val("../results/query_dependencies_benchmark_result.json");
  CLI11_PARSE(app, argc, argv);

  std::filesystem::create_directories("./temp");
  DependencyGraph baseline_graph("./temp/data/baseline", kCreate, std::numeric_limits<std::size_t>::max());
  DependencyGraph test_graph("./temp/data/test", kCreate, opt.memory_limit * MiB);
  PackageLoader baseline_loader(baseline_graph);
  PackageLoader test_loader(test_graph);
  if (!baseline_loader.load_dataset_file(opt.dataset_file, true)) return 1;
  if (!test_loader.load_dataset_file(opt.dataset_file, true)) return 1;

  print("Flushing to disk... ");
  auto flush_time = measure_time<std::chrono::milliseconds>([&] { test_graph.flush_buffer(); });
  println("Done. ({:.3f} s)", flush_time.count() / 1000.0);
  println("Total {} packages, {} versions, {} dependencies.",
          test_graph.package_count(), test_graph.version_count(), test_graph.dependency_count());
  print("Syncing to GPU... ");
  auto sync_time = measure_time<std::chrono::milliseconds>([&] { test_graph.sync_gpu(); });
  println("Done. ({:.3f} s)", sync_time.count() / 1000.0);

  std::vector<std::string_view> to_query;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<std::size_t> dist(0, test_graph.package_count() - 1);
  while (to_query.size() < opt.trials) {
    auto pview = test_graph.get_package(dist(gen));
    if (pview.versions().empty()) continue;
    to_query.emplace_back(pview.name);
  }

  println("=== Query Dependencies Benchmark ===");
  println("Testing {} packages with max_depth={}, total {} tests...",
          opt.trials, opt.max_depth, opt.trials * opt.max_depth * 3);
  nlohmann::ordered_json result;
  result["title"] = "Query Dependencies Benchmark";
  result["time"] = now_iso8601();
  result["trials"] = opt.trials;
  result["max_depth"] = opt.max_depth;
  result["memory_limit"] = std::format("{} MiB", opt.memory_limit);
  result["baseline_results"] = nlohmann::ordered_json::array();
  result["gpu_results"] = nlohmann::ordered_json::array();
  result["memory_limit_results"] = nlohmann::ordered_json::array();

  std::vector<std::vector<std::size_t>>
    baseline_times(opt.max_depth), gpu_times(opt.max_depth), memory_limit_times(opt.max_depth);
  for (auto depth = 1; depth <= opt.max_depth; ++depth) {
    println("Testing depth={}...", depth);
    for (const auto &name : to_query) {
      auto [_, time] = measure_time<std::chrono::microseconds>([&baseline_graph, &name, depth] {
        return baseline_graph.query_dependencies_on_buffer(name, "", "", depth);
      });
      baseline_times[depth - 1].emplace_back(time.count());
    }
    auto &baseline_result = result["baseline_results"].emplace_back();
    baseline_result["depth"] = depth;
    println("Baseline       tests completed. Average {:.3f} ms per query.",
            analyze_times(baseline_result, baseline_times[depth - 1], opt.trials));

    for (const auto &name : to_query) {
      auto [_, time] = measure_time<std::chrono::microseconds>([&test_graph, &name, depth] {
        return test_graph.query_dependencies(name, "", "", depth, true);
      });
      gpu_times[depth - 1].emplace_back(time.count());
    }
    auto &gpu_result = result["gpu_results"].emplace_back();
    gpu_result["depth"] = depth;
    println("GPU            tests completed. Average {:.3f} ms per query.",
            analyze_times(gpu_result, gpu_times[depth - 1], opt.trials));

    for (const auto &name : to_query) {
      auto [_, time] = measure_time<std::chrono::microseconds>([&test_graph, &name, depth] {
        return test_graph.query_dependencies(name, "", "", depth, false);
      });
      memory_limit_times[depth - 1].emplace_back(time.count());
    }
    auto &memory_limit_result = result["memory_limit_results"].emplace_back();
    memory_limit_result["depth"] = depth;
    println("Memory-limited tests completed. Average {:.3f} ms per query.",
            analyze_times(memory_limit_result, memory_limit_times[depth - 1], opt.trials));
  }
  println("All tests completed.");
  println("====================================");

  println("Cleaning up...");
  baseline_graph.close();
  test_graph.close();
  std::filesystem::remove_all("./temp");
  std::filesystem::create_directories(std::filesystem::path(opt.output_file).parent_path());
  std::ofstream(opt.output_file) << result.dump(2);
  return 0;
}
