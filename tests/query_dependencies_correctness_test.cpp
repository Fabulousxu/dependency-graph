#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
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

struct Option {
  std::string dataset_file;
  std::size_t trials;
  std::size_t max_depth;
  std::string output_file;
};

int main(int argc, char *argv[]) {
  Option opt;
  CLI::App app{};
  app.add_option("--dataset", opt.dataset_file)->required()->check(CLI::ExistingFile);
  app.add_option("--trials", opt.trials)->required()->check(CLI::PositiveNumber);
  app.add_option("--max-depth", opt.max_depth)->required()->check(CLI::PositiveNumber);
  app.add_option("--output", opt.output_file)
     ->default_val("../results/query_dependencies_correctness_test_result.json");
  CLI11_PARSE(app, argc, argv);

  std::string data_dir = "./tempdata";
  std::filesystem::create_directories(data_dir);
  auto baseline_graph =
    std::make_unique<DependencyGraph>(data_dir + "/baseline", std::numeric_limits<std::size_t>::max());
  auto test_graph = std::make_unique<DependencyGraph>(data_dir + "/test", 0);
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
    opt.trials, opt.max_depth, opt.trials * opt.max_depth);
  json result;
  auto now = std::chrono::system_clock::now();
  std::chrono::sys_time tp{std::chrono::time_point_cast<std::chrono::milliseconds>(now)};
  result["title"] = "Query Dependencies Correctness Test";
  result["time"] = std::format("{:%Y-%m-%dT%H:%M:%SZ}", tp);
  result["trials"] = opt.trials;
  result["max_depth"] = opt.max_depth;
  result["total_test_count"] = opt.trials * opt.max_depth;
  result["passed_test_count"] = 0;
  result["failed_test_count"] = 0;
  result["failed_tests"] = json::array();

  std::size_t passed_cnt = 0, tested_cnt = 0;
  for (auto depth = 1; depth <= opt.max_depth; depth++) {
    for (const auto &name : to_query) {
      auto baseline_result = baseline_graph->query_dependencies_on_buffer(name, "", "", depth);
      auto disk_result = test_graph->query_dependencies(name, "", "", depth, false);
      auto gpu_result = test_graph->query_dependencies(name, "", "", depth, true);

      for (auto level = 0; level < depth; level++) {
        const auto &[baseline_direct, baseline_or] = baseline_result[level];
        const auto &[disk_direct, disk_or] = disk_result[level];
        const auto &[gpu_direct, gpu_or] = gpu_result[level];

        if (baseline_direct.size() != disk_direct.size() || baseline_direct.size() != gpu_direct.size()
          || baseline_or.size() != disk_or.size() || baseline_or.size() != gpu_or.size()) {
          auto &failure = result["failed_tests"].emplace_back();
          failure["package_name"] = name;
          failure["depth"] = depth;
          failure["failed_level"] = level;
          if (baseline_direct.size() != disk_direct.size()) {
            failure["reason"] = "Direct dependency count mismatch between baseline and disk.";
            failure["baseline_level_direct_dependency_count"] = baseline_direct.size();
            failure["disk_level_direct_dependency_count"] = disk_direct.size();
          } else if (baseline_direct.size() != gpu_direct.size()) {
            failure["reason"] = "Direct dependency count mismatch between baseline and GPU.";
            failure["baseline_level_direct_dependency_count"] = baseline_direct.size();
            failure["gpu_level_direct_dependency_count"] = gpu_direct.size();
          } else if (baseline_or.size() != disk_or.size()) {
            failure["reason"] = "Or dependency group count mismatch between baseline and disk.";
            failure["baseline_level_or_dependency_group_count"] = baseline_or.size();
            failure["disk_level_or_dependency_group_count"] = disk_or.size();
          } else if (baseline_or.size() != gpu_or.size()) {
            failure["reason"] = "Or dependency group count mismatch between baseline and GPU.";
            failure["baseline_level_or_dependency_group_count"] = baseline_or.size();
            failure["gpu_level_or_dependency_group_count"] = gpu_or.size();
          }
          goto test_failed;
        }

        std::unordered_set<DependencyItem> baseline_direct_set{baseline_direct.begin(), baseline_direct.end()};
        std::unordered_set<DependencyItem> disk_direct_set{disk_direct.begin(), disk_direct.end()};
        std::unordered_set<DependencyItem> gpu_direct_set{gpu_direct.begin(), gpu_direct.end()};
        if (baseline_direct_set != disk_direct_set || baseline_direct_set.size() != disk_direct_set.size()) {
          auto &failure = result["failed_tests"].emplace_back();
          failure["package_name"] = name;
          failure["depth"] = depth;
          failure["failed_level"] = level;
          if (baseline_direct_set != disk_direct_set)
            failure["reason"] = "Direct dependencies mismatch between baseline and disk.";
          else failure["reason"] = "Direct dependencies mismatch between baseline and GPU.";
          goto test_failed;
        }

        std::vector<std::unordered_set<DependencyItem>> baseline_or_set, disk_or_set, gpu_or_set;
        for (auto group = 0; group < baseline_or.size(); group++) {
          const auto &baseline_group = baseline_or[group];
          const auto &disk_group = disk_or[group];
          const auto &gpu_group = gpu_or[group];
          baseline_or_set.emplace_back(baseline_group.begin(), baseline_group.end());
          disk_or_set.emplace_back(disk_group.begin(), disk_group.end());
          gpu_or_set.emplace_back(gpu_group.begin(), gpu_group.end());
        }
        for (const auto &baseline_set : baseline_or_set) {
          bool correct_disk = false, correct_gpu = false;
          for (const auto &disk_set : disk_or_set)
            if (baseline_set == disk_set) {
              correct_disk = true;
              break;
            }
          if (correct_disk)
            for (const auto &gpu_set : gpu_or_set)
              if (baseline_set == gpu_set) {
                correct_gpu = true;
                break;
              }
          if (!correct_disk || !correct_gpu) {
            auto &failure = result["failed_tests"].emplace_back();
            failure["package_name"] = name;
            failure["depth"] = depth;
            failure["failed_level"] = level;
            if (!correct_disk) failure["reason"] = "Or dependencies mismatch between baseline and disk.";
            else failure["reason"] = "Or dependencies mismatch between baseline and GPU.";
            goto test_failed;
          }
        }
      }

      passed_cnt++;
      goto test_progress;
    test_failed:
      std::cout << std::format("Test failed for package: {}, depth={}.\n", name, depth);
    test_progress:
      if (++tested_cnt % 100 == 0)
        std::cout << std::format("{}/{} tests completed. Passed: {}, Failed: {}.\n",
          tested_cnt, opt.trials * opt.max_depth, passed_cnt, tested_cnt - passed_cnt);
    }
  }

  result["passed_test_count"] = passed_cnt;
  result["failed_test_count"] = opt.trials * opt.max_depth - passed_cnt;
  std::cout << std::format("All tests completed. Total: {}, Passed: {}, Failed: {}.\n",
    opt.trials * opt.max_depth, passed_cnt, opt.trials * opt.max_depth - passed_cnt);
  std::cout << "===========================================\n";

  baseline_graph.reset();
  test_graph.reset();
  std::filesystem::remove_all("./tempdata");
  std::filesystem::create_directories(std::filesystem::path(opt.output_file).parent_path());
  std::ofstream{opt.output_file} << result.dump(2);
  return result["failed_test_count"].get<std::size_t>() != 0;
}
