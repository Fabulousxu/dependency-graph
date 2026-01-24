#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>
#include "config.hpp"
#include "dependency_graph.hpp"
#include "package_loader.hpp"
#include "util.hpp"

struct Option {
  std::string dataset_file;
  std::size_t trials;
  std::size_t max_depth;
  std::string output_file;
};

int main(int argc, char *argv[]) {
  Option opt;
  CLI::App app;
  app.add_option("--dataset", opt.dataset_file)->required()->check(CLI::ExistingFile);
  app.add_option("--trials", opt.trials)->required()->check(CLI::PositiveNumber);
  app.add_option("--max-depth", opt.max_depth)->required()->check(CLI::PositiveNumber);
  app.add_option("--output", opt.output_file)
     ->default_val("../results/query_dependencies_correctness_test_result.json");
  CLI11_PARSE(app, argc, argv);

  std::filesystem::create_directories("./temp");
  DependencyGraph baseline_graph("./temp/data/baseline", kCreate, std::numeric_limits<std::size_t>::max());
  DependencyGraph test_graph("./temp/data/test", kCreate, 0);
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

  println("=== Query Dependencies Correctness Test ===");
  println("Testing {} packages with max_depth={}, total {} tests...",
          opt.trials, opt.max_depth, opt.trials * opt.max_depth);
  nlohmann::ordered_json result;
  result["title"] = "Query Dependencies Correctness Test";
  result["time"] = now_iso8601();
  result["trials"] = opt.trials;
  result["max_depth"] = opt.max_depth;
  result["total_test_count"] = opt.trials * opt.max_depth;
  result["passed_test_count"] = 0;
  result["failed_test_count"] = 0;
  result["failed_tests"] = nlohmann::ordered_json::array();

  std::size_t passed_cnt = 0, tested_cnt = 0;
  for (auto depth = 1; depth <= opt.max_depth; ++depth) {
    for (const auto &name : to_query) {
      auto baseline_result = baseline_graph.query_dependencies_on_buffer(name, "", "", depth);
      auto disk_result = test_graph.query_dependencies(name, "", "", depth, false);
      auto gpu_result = test_graph.query_dependencies(name, "", "", depth, true);

      for (auto level = 0; level < depth; ++level) {
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

        std::unordered_set baseline_direct_set(baseline_direct.begin(), baseline_direct.end());
        std::unordered_set disk_direct_set(disk_direct.begin(), disk_direct.end());
        std::unordered_set gpu_direct_set(gpu_direct.begin(), gpu_direct.end());
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
      println("Test failed for package: {}, depth={}.", name, depth);
    test_progress:
      if (++tested_cnt % 100 == 0)
        println("Progress: {}/{} tests completed. Passed: {}, Failed: {}.",
                tested_cnt, opt.trials * opt.max_depth, passed_cnt, tested_cnt - passed_cnt);
    }
  }

  result["passed_test_count"] = passed_cnt;
  result["failed_test_count"] = opt.trials * opt.max_depth - passed_cnt;
  println("All tests completed. Total: {}, Passed: {}, Failed: {}.",
          opt.trials * opt.max_depth, passed_cnt, opt.trials * opt.max_depth - passed_cnt);
  println("===========================================");

  println("Cleaning up...");
  baseline_graph.close();
  test_graph.close();
  std::filesystem::remove_all("./temp");
  std::filesystem::create_directories(std::filesystem::path(opt.output_file).parent_path());
  std::ofstream(opt.output_file) << result.dump(2);
  return opt.trials * opt.max_depth - passed_cnt > 0;
}
