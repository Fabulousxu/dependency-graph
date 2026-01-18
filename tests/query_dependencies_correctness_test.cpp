#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>
#include "dependency_graph.hpp"
#include "package_loader.hpp"

using json = nlohmann::ordered_json;

json query_dependencies_correctness_test(DependencyGraph &graph, std::size_t test_pkg_cnt, std::size_t max_depth) {
  std::cout << "=== Query Dependencies Correctness Test ===\n";
  std::cout << std::format("Testing {} packages with max_depth={}, total {} tests...\n",
    test_pkg_cnt, max_depth, test_pkg_cnt * max_depth);
  json result;
  auto now = std::chrono::system_clock::now();
  std::chrono::sys_time tp{std::chrono::time_point_cast<std::chrono::milliseconds>(now)};
  result["title"] = "Query Dependencies Correctness Test";
  result["start_at"] = std::format("{:%Y-%m-%dT%H:%M:%SZ}", tp);
  result["test_package_count"] = test_pkg_cnt;
  result["max_depth"] = max_depth;
  result["total_test_count"] = test_pkg_cnt * max_depth;
  result["passed_test_count"] = 0;
  result["failed_test_count"] = 0;
  result["failed_tests"] = json::array();

  std::vector<std::string_view> to_query;
  std::random_device rd;
  std::mt19937 gen{rd()};
  std::uniform_int_distribution<std::size_t> dist{0, graph.package_count() - 1};
  while (to_query.size() < test_pkg_cnt) {
    const auto &pnode = graph.get_package(dist(gen));
    if (pnode.version_ids.empty()) continue;
    to_query.emplace_back(pnode.name);
  }

  std::size_t passed_cnt = 0, tested_cnt = 0;
  for (auto depth = 1; depth <= max_depth; depth++) {
    for (const auto &name : to_query) {
      auto cpu_result = graph.query_dependencies(name, "", "", depth, false);
      auto gpu_result = graph.query_dependencies(name, "", "", depth, true);
      for (auto level = 0; level < depth; level++) {
        const auto &[cpu_direct, cpu_or] = cpu_result[level];
        const auto &[gpu_direct, gpu_or] = gpu_result[level];
        if (cpu_direct.size() != gpu_direct.size()) {
          auto &failure = result["failed_tests"].emplace_back();
          failure["package_name"] = name;
          failure["depth"] = depth;
          failure["failed_level"] = level;
          failure["reason"] = "Direct dependency count mismatch.";
          failure["cpu_level_direct_dependency_count"] = cpu_direct.size();
          failure["gpu_level_direct_dependency_count"] = gpu_direct.size();
          goto test_failed;
        }
        if (cpu_or.size() != gpu_or.size()) {
          auto &failure = result["failed_tests"].emplace_back();
          failure["package_name"] = name;
          failure["depth"] = depth;
          failure["failed_level"] = level;
          failure["reason"] = "Or dependency group count mismatch.";
          failure["cpu_level_or_dependency_group_count"] = cpu_or.size();
          failure["gpu_level_or_dependency_group_count"] = gpu_or.size();
          goto test_failed;
        }
        std::unordered_set<DependencyItem> cpu_direct_set{cpu_direct.begin(), cpu_direct.end()};
        std::unordered_set<DependencyItem> gpu_direct_set{gpu_direct.begin(), gpu_direct.end()};
        if (cpu_direct_set != gpu_direct_set) {
          auto &failure = result["failed_tests"].emplace_back();
          failure["package_name"] = name;
          failure["depth"] = depth;
          failure["failed_level"] = level;
          failure["reason"] = "Direct dependencies mismatch.";
          goto test_failed;
        }
        std::vector<std::unordered_set<DependencyItem>> cpu_or_sets, gpu_or_sets;
        for (auto group = 0; group < cpu_or.size(); group++) {
          const auto &cpu_group = cpu_or[group];
          const auto &gpu_group = gpu_or[group];
          cpu_or_sets.emplace_back(cpu_group.begin(), cpu_group.end());
          gpu_or_sets.emplace_back(gpu_group.begin(), gpu_group.end());
        }
        for (const auto &cpu_or_set : cpu_or_sets) {
          bool correct = false;
          for (const auto &gpu_or_set : gpu_or_sets)
            if (cpu_or_set == gpu_or_set) {
              correct = true;
              break;
            }
          if (!correct) {
            auto &failure = result["failed_tests"].emplace_back();
            failure["package_name"] = name;
            failure["depth"] = depth;
            failure["failed_level"] = level;
            failure["reason"] = "Or dependencies mismatch.";
            goto test_failed;
          }
        }
      }
      passed_cnt++;
      goto test_progress;
    test_failed:
      std::cout << std::format("Test failed for package: {}, depth={}.\n", name, depth);
    test_progress:
      if (++tested_cnt % 1000 == 0)
        std::cout << std::format("{}/{} tests completed. Passed: {}, Failed: {}.\n",
          tested_cnt, test_pkg_cnt * max_depth, passed_cnt, tested_cnt - passed_cnt);
    }
  }
  result["passed_test_count"] = passed_cnt;
  result["failed_test_count"] = test_pkg_cnt * max_depth - passed_cnt;
  std::cout << std::format("All tests completed. Total: {}, Passed: {}, Failed: {}.\n",
    test_pkg_cnt * max_depth, passed_cnt, test_pkg_cnt * max_depth - passed_cnt);
  std::cout << "===========================================\n";
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
  CLI::App app{};
  app.add_option("--dataset", opt.dataset_filename)->required()->check(CLI::ExistingFile);
  app.add_option("--test-package-count", opt.test_package_count)->required()->check(CLI::PositiveNumber);
  app.add_option("--max-depth", opt.max_depth)->required()->check(CLI::PositiveNumber);
  app.add_option("--output", opt.output_directory)->default_val("../results");
  CLI11_PARSE(app, argc, argv);

  DependencyGraph graph;
  PackageLoader loader(graph);
  if (!loader.load_from_dataset_file(opt.dataset_filename, true)) return 1;
  graph.build_gpu_graph(true);
  auto result = query_dependencies_correctness_test(graph, opt.test_package_count, opt.max_depth);
  std::filesystem::create_directory(opt.output_directory);
  std::ofstream result_file(opt.output_directory + "/correctness_test_result.json");
  result_file << result.dump(2);
  return result["failed_test_count"].get<std::size_t>() != 0;
}
