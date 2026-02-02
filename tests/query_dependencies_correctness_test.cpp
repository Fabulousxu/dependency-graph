#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>
#include "dependency_graph.hpp"
#include "package_loader.hpp"
#include "util.hpp"

bool compare_result(const DependencyResult &baseline, const DependencyResult &test, nlohmann::ordered_json &failure,
                    std::string_view baseline_name = "", std::string_view test_name = "") {
  for (auto level = 0; level < baseline.size(); ++level) {
    const auto &[bdirect, bor] = baseline[level];
    const auto &[tdirect, tor] = test[level];
    if (bdirect.size() != tdirect.size()) {
      failure["failed_level"] = level;
      failure["reason"] = std::format("Direct dependency count mismatch between {} and {}.", baseline_name, test_name);
      failure[std::format("{}_direct_dependency_count", baseline_name)] = bdirect.size();
      failure[std::format("{}_direct_dependency_count", test_name)] = tdirect.size();
      return false;
    }
    if (bor.size() != tor.size()) {
      failure["failed_level"] = level;
      failure["reason"] = std::format("Or dependency group count mismatch between {} and {}.",
                                      baseline_name, test_name);
      failure[std::format("{}_or_dependency_group_count", baseline_name)] = bor.size();
      failure[std::format("{}_or_dependency_group_count", test_name)] = tor.size();
      return false;
    }

    std::unordered_set bdirect_set(bdirect.begin(), bdirect.end());
    std::unordered_set tdirect_set(tdirect.begin(), tdirect.end());
    if (bdirect_set != tdirect_set) {
      failure["failed_level"] = level;
      failure["reason"] = std::format("Direct dependencies mismatch between {} and {}.", baseline_name, test_name);
      return false;
    }

    std::vector<std::unordered_set<DependencyInfo>> bor_set, tor_set;
    for (auto group = 0; group < bor.size(); group++) {
      bor_set.emplace_back(bor[group].begin(), bor[group].end());
      tor_set.emplace_back(tor[group].begin(), tor[group].end());
    }
    for (const auto &bgroup_set : bor_set) {
      bool correct = false;
      for (const auto &tgroup_set : tor_set) {
        if (bgroup_set == tgroup_set) {
          correct = true;
          break;
        }
      }
      if (!correct) {
        failure["failed_level"] = level;
        failure["reason"] = std::format("Or dependency groups mismatch between {} and {}.", baseline_name, test_name);
        return false;
      }
    }
  }
  return true;
}

struct Option {
  std::string dataset_file;
  std::string load_dir;
  std::size_t trials;
  std::size_t max_depth;
  std::string output_file;
};

int main(int argc, char *argv[]) {
  Option opt;
  CLI::App app;
  app.add_option("--dataset", opt.dataset_file)->required()->check(CLI::ExistingFile);
  app.add_option("--load-dir", opt.load_dir)->check(CLI::ExistingDirectory);
  app.add_option("--trials", opt.trials)->required()->check(CLI::PositiveNumber);
  app.add_option("--max-depth", opt.max_depth)->required()->check(CLI::PositiveNumber);
  app.add_option("--output", opt.output_file)->required();
  CLI11_PARSE(app, argc, argv);

  std::filesystem::create_directories("./temp");
  DependencyGraph inmem_graph(std::numeric_limits<std::size_t>::max());
  DependencyGraph test_graph(0);
  DependencyGraph load_graph;
  if (!inmem_graph.create("./temp/data/in-memory")) {
    println("Failed to create DependencyGraph at directory: {}", "./temp/data/in-memory");
    return 1;
  }
  if (!test_graph.create("./temp/data/test")) {
    println("Failed to create DependencyGraph at directory: {}", "./temp/data/test");
    return 1;
  }
  if (!opt.load_dir.empty() && !load_graph.load(opt.load_dir)) {
    println("Failed to load DependencyGraph from directory: {}", opt.load_dir);
    return 1;
  }

  PackageLoader inmem_loader(inmem_graph);
  PackageLoader test_loader(test_graph);
  if (!inmem_loader.load_dataset_file(opt.dataset_file, true)) return 1;
  if (!test_loader.load_dataset_file(opt.dataset_file, true)) return 1;
  test_graph.flush_buffer();
  println("Total {} packages, {} versions, {} dependencies.",
          test_graph.package_count(), test_graph.version_count(), test_graph.dependency_count());
  print("Building cache on GPU... ");
  auto sync_time = measure_time<std::chrono::milliseconds>([&] { test_graph.build_cache(); });
  println("Done. ({} ms)", sync_time.count());

  std::vector<std::string> to_query;
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
  result["package_count"] = test_graph.package_count();
  result["version_count"] = test_graph.version_count();
  result["dependency_count"] = test_graph.dependency_count();
  result["test_load"] = !opt.load_dir.empty();
  result["trials"] = opt.trials;
  result["max_depth"] = opt.max_depth;
  result["total_test_count"] = opt.trials * opt.max_depth;
  result["passed_test_count"] = 0;
  result["failed_test_count"] = 0;
  result["failed_tests"] = nlohmann::ordered_json::array();

  std::size_t passed_cnt = 0, tested_cnt = 0;
  for (auto depth = 1; depth <= opt.max_depth; ++depth) {
    for (const auto &name : to_query) {
      auto inmem_result = inmem_graph.query_dependencies_on_buffer(name, "", "", depth);
      auto disk_result = test_graph.query_dependencies(name, "", "", depth, false);
      auto gpu_result = test_graph.query_dependencies(name, "", "", depth, true);

      nlohmann::ordered_json failure;
      failure["package_name"] = name;
      failure["depth"] = depth;
      bool succ = compare_result(inmem_result, disk_result, failure, "baseline", "disk");
      if (succ) succ = compare_result(inmem_result, gpu_result, failure, "baseline", "GPU");
      if (!opt.load_dir.empty() && succ) {
        auto load_result = load_graph.query_dependencies(name, "", "", depth, false);
        succ = compare_result(inmem_result, load_result, failure, "baseline", "loaded");
      }

      if (!succ) {
        println("Test failed for package: {}, depth={}.", name, depth);
        result["failed_tests"].emplace_back(failure);
      } else ++passed_cnt;
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
  inmem_graph.close();
  test_graph.close();
  std::filesystem::remove_all("./temp");
  std::filesystem::create_directories(std::filesystem::path(opt.output_file).parent_path());
  std::ofstream(opt.output_file) << result.dump(2);
  return opt.trials * opt.max_depth - passed_cnt > 0;
}
