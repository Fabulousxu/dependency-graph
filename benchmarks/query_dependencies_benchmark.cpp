#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
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

using json = nlohmann::ordered_json;

double analyze_times(json &result, std::vector<std::size_t> &times, std::size_t trials) {
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
  CLI::App app{};
  app.add_option("--dataset", opt.dataset_file)->required()->check(CLI::ExistingFile);
  app.add_option("--trials", opt.trials)->required()->check(CLI::PositiveNumber);
  app.add_option("--max-depth", opt.max_depth)->required()->check(CLI::PositiveNumber);
  app.add_option("--memory-limit", opt.memory_limit)->required()->check(CLI::PositiveNumber);
  app.add_option("--output", opt.output_file)->default_val("../results/query_dependencies_benchmark_result.json");
  CLI11_PARSE(app, argc, argv);

  std::string data_dir = "./tempdata";
  std::filesystem::create_directories(data_dir);
  auto baseline_graph =
    std::make_unique<DependencyGraph>(data_dir + "/baseline", kCreate, std::numeric_limits<std::size_t>::max());
  auto test_graph = std::make_unique<DependencyGraph>(data_dir + "/test", kCreate, opt.memory_limit * 1024 * 1024);
  PackageLoader baseline_loader{*baseline_graph};
  PackageLoader test_loader{*test_graph};
  if (!baseline_loader.load_from_dataset_file(opt.dataset_file, true)) return 1;
  if (!test_loader.load_from_dataset_file(opt.dataset_file, true)) return 1;
  std::cout << "Flushing to disk... ";
  auto flush_start = std::chrono::high_resolution_clock::now();
  test_graph->flush_to_disk();
  auto flush_end = std::chrono::high_resolution_clock::now();
  auto flush_time = std::chrono::duration_cast<std::chrono::milliseconds>(flush_end - flush_start);
  std::cout << std::format("Done. ({} ms)\n", flush_time.count());
  std::cout << std::format(" Total {} packages, {} versions, {} dependencies.\n",
    test_graph->package_count(), test_graph->version_count(), test_graph->dependency_count());
  std::cout << "Syncing to GPU... ";
  auto sync_start = std::chrono::high_resolution_clock::now();
  test_graph->sync_to_gpu();
  auto sync_end = std::chrono::high_resolution_clock::now();
  auto sync_time = std::chrono::duration_cast<std::chrono::milliseconds>(sync_end - sync_start);
  std::cout << std::format("Done. ({} ms)\n", sync_time.count());

  std::vector<std::string_view> to_query;
  std::random_device rd;
  std::mt19937 gen{rd()};
  std::uniform_int_distribution<std::size_t> dist{0, test_graph->package_count() - 1};
  while (to_query.size() < opt.trials) {
    auto pview = test_graph->get_package(dist(gen));
    if (pview.versions().empty()) continue;
    to_query.emplace_back(pview.name);
  }

  std::cout << "=== Query Dependencies Correctness Test ===\n";
  std::cout << std::format("Testing {} packages with max_depth={}, total {} tests...\n",
    opt.trials, opt.max_depth, opt.trials * opt.max_depth * 3);
  json result;
  auto now = std::chrono::system_clock::now();
  std::chrono::sys_time tp{std::chrono::time_point_cast<std::chrono::milliseconds>(now)};
  result["title"] = "Query Dependencies Benchmark";
  result["time"] = std::format("{:%Y-%m-%dT%H:%M:%SZ}", tp);
  result["trials"] = opt.trials;
  result["max_depth"] = opt.max_depth;
  result["max_memory"] = std::format("{} MB", opt.memory_limit);
  result["baseline_results"] = json::array();
  result["gpu_results"] = json::array();
  result["memory_limit_results"] = json::array();

  std::vector<std::vector<std::size_t>>
    baseline_times{opt.max_depth}, gpu_times{opt.max_depth}, memory_limit_times{opt.max_depth};
  for (auto depth = 1; depth <= opt.max_depth; depth++) {
    std::cout << std::format("Testing depth={}...\n", depth);
    for (const auto &name : to_query) {
      auto start = std::chrono::high_resolution_clock::now();
      auto _ = baseline_graph->query_dependencies_on_buffer(name, "", "", depth);
      auto end = std::chrono::high_resolution_clock::now();
      auto time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
      baseline_times[depth - 1].emplace_back(time.count());
    }
    auto &baseline_result = result["baseline_results"].emplace_back();
    baseline_result["depth"] = depth;
    std::cout << std::format("Baseline       tests completed. Average {:.3f} ms per query.\n",
      analyze_times(baseline_result, baseline_times[depth - 1], opt.trials));

    for (const auto &name : to_query) {
      auto start = std::chrono::high_resolution_clock::now();
      auto _ = test_graph->query_dependencies(name, "", "", depth, true);
      auto end = std::chrono::high_resolution_clock::now();
      auto time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
      gpu_times[depth - 1].emplace_back(time.count());
    }
    auto &gpu_result = result["gpu_results"].emplace_back();
    gpu_result["depth"] = depth;
    std::cout << std::format("GPU            tests completed. Average {:.3f} ms per query.\n",
      analyze_times(gpu_result, gpu_times[depth - 1], opt.trials));

    for (const auto &name : to_query) {
      auto start = std::chrono::high_resolution_clock::now();
      auto _ = test_graph->query_dependencies(name, "", "", depth, false);
      auto end = std::chrono::high_resolution_clock::now();
      auto time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
      memory_limit_times[depth - 1].emplace_back(time.count());
    }
    auto &memory_limit_result = result["memory_limit_results"].emplace_back();
    memory_limit_result["depth"] = depth;
    std::cout << std::format("Memory-limited tests completed. Average {:.3f} ms per query.\n",
      analyze_times(memory_limit_result, memory_limit_times[depth - 1], opt.trials));
  }
  std::cout << "====================================" << std::endl;

  baseline_graph.reset();
  test_graph.reset();
  std::filesystem::remove_all("./tempdata");
  std::filesystem::create_directories(std::filesystem::path(opt.output_file).parent_path());
  std::ofstream{opt.output_file} << result.dump(2);
  return 0;
}
