#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
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

json query_dependencies_benchmark(DependencyGraph &graph, std::size_t test_pkg_cnt, std::size_t max_depth) {
  std::cout << "=== Query Dependencies Benchmark ===" << std::endl;
  std::cout << std::format("Testing {} packages with max_depth={}, total {} tests...\n",
    test_pkg_cnt, max_depth, test_pkg_cnt * max_depth * 2);
  json result;
  auto now = std::chrono::system_clock::now();
  std::chrono::sys_time tp{std::chrono::time_point_cast<std::chrono::milliseconds>(now)};
  result["title"] = "Query Dependencies Benchmark";
  result["start_at"] = std::format("{:%Y-%m-%dT%H:%M:%SZ}", tp);
  result["test_package_count"] = test_pkg_cnt;
  result["max_depth"] = max_depth;
  result["cpu_results"] = json::array();
  result["gpu_results"] = json::array();

  std::vector<std::string_view> to_query;
  std::random_device rd;
  std::mt19937 gen{rd()};
  std::uniform_int_distribution<std::size_t> dist{0, graph.package_count() - 1};
  while (to_query.size() < test_pkg_cnt) {
    const auto &pnode = graph.get_package(dist(gen));
    if (pnode.version_ids.empty()) continue;
    to_query.emplace_back(pnode.name);
  }

  std::vector<std::vector<std::size_t>> cpu_times{max_depth}, gpu_times{max_depth};
  for (auto depth = 1; depth <= max_depth; depth++) {
    for (const auto &name : to_query) {
      auto start = std::chrono::high_resolution_clock::now();
      auto _ = graph.query_dependencies(name, "", "", depth, false);
      auto end = std::chrono::high_resolution_clock::now();
      auto time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
      cpu_times[depth - 1].emplace_back(time.count());
    }
    std::ranges::sort(cpu_times[depth - 1]);
    auto total_time = std::accumulate(cpu_times[depth - 1].begin(), cpu_times[depth - 1].end(), 0ull);
    auto &cpu_result = result["cpu_results"].emplace_back();
    cpu_result["depth"] = depth;
    cpu_result["avg"] = std::format("{:.3f} ms", total_time / test_pkg_cnt / 1000.0);
    cpu_result["min"] = std::format("{:.3f} ms", cpu_times[depth - 1].front() / 1000.0);
    cpu_result["max"] = std::format("{:.3f} ms", cpu_times[depth - 1].back() / 1000.0);
    cpu_result["p50"] = std::format("{:.3f} ms", cpu_times[depth - 1][test_pkg_cnt / 2] / 1000.0);
    cpu_result["p75"] = std::format("{:.3f} ms", cpu_times[depth - 1][test_pkg_cnt * 3 / 4] / 1000.0);
    cpu_result["p90"] = std::format("{:.3f} ms", cpu_times[depth - 1][test_pkg_cnt * 9 / 10] / 1000.0);
    cpu_result["p95"] = std::format("{:.3f} ms", cpu_times[depth - 1][test_pkg_cnt * 19 / 20] / 1000.0);
    cpu_result["p99"] = std::format("{:.3f} ms", cpu_times[depth - 1][test_pkg_cnt * 99 / 100] / 1000.0);
    std::cout << std::format("CPU tests with depth={} completed. Average {:.3f} ms per query.\n",
      depth, total_time / test_pkg_cnt / 1000.0);

    for (const auto &name : to_query) {
      auto start = std::chrono::high_resolution_clock::now();
      auto _ = graph.query_dependencies(name, "", "", depth, true);
      auto end = std::chrono::high_resolution_clock::now();
      auto time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
      gpu_times[depth - 1].emplace_back(time.count());
    }
    std::ranges::sort(gpu_times[depth - 1]);
    total_time = std::accumulate(gpu_times[depth - 1].begin(), gpu_times[depth - 1].end(), 0ull);
    auto &gpu_result = result["gpu_results"].emplace_back();
    gpu_result["depth"] = depth;
    gpu_result["avg"] = std::format("{:.3f} us", total_time / test_pkg_cnt / 1000.0);
    gpu_result["min"] = std::format("{:.3f} us", gpu_times[depth - 1].front() / 1000.0);
    gpu_result["max"] = std::format("{:.3f} us", gpu_times[depth - 1].back() / 1000.0);
    gpu_result["p50"] = std::format("{:.3f} us", gpu_times[depth - 1][test_pkg_cnt / 2] / 1000.0);
    gpu_result["p75"] = std::format("{:.3f} us", gpu_times[depth - 1][test_pkg_cnt * 3 / 4] / 1000.0);
    gpu_result["p90"] = std::format("{:.3f} us", gpu_times[depth - 1][test_pkg_cnt * 9 / 10] / 1000.0);
    gpu_result["p95"] = std::format("{:.3f} us", gpu_times[depth - 1][test_pkg_cnt * 19 / 20] / 1000.0);
    gpu_result["p99"] = std::format("{:.3f} us", gpu_times[depth - 1][test_pkg_cnt * 99 / 100] / 1000.0);
    std::cout << std::format("GPU tests with depth={} completed. Average {:.3f} ms per query.\n",
      depth, total_time / test_pkg_cnt / 1000.0);
  }
  std::cout << "====================================" << std::endl;
  return result;
}

struct Option {
  std::string dataset_filename;
  std::size_t test_package_count;
  std::size_t max_depth;
  std::string output_directory;
};

int main(int argc, char *argv[]) {
  Option opt;
  CLI::App app;
  app.add_option("--dataset", opt.dataset_filename)->required()->check(CLI::ExistingFile);
  app.add_option("--test-package-count", opt.test_package_count)->required()->check(CLI::PositiveNumber);
  app.add_option("--max-depth", opt.max_depth)->required()->check(CLI::PositiveNumber);
  app.add_option("--output", opt.output_directory)->default_val("../results");
  CLI11_PARSE(app, argc, argv);

  DependencyGraph graph;
  PackageLoader loader(graph);
  if (!loader.load_from_dataset_file(opt.dataset_filename, true)) return 1;
  graph.build_gpu_graph(true);
  auto result = query_dependencies_benchmark(graph, opt.test_package_count, opt.max_depth);
  std::filesystem::create_directory(opt.output_directory);
  std::ofstream result_file(opt.output_directory + "/query_dependencies_benchmark_result.json");
  result_file << result.dump(2);
  return 0;
}
