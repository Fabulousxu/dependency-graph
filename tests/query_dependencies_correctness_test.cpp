#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>
#include "json_serialization.hpp"
#include "mgxpgraph.hpp"
#include "utils.hpp"
#include "xpgraph.hpp"

namespace xpg {

class QueryDependenciesCorrectnessTest {
  using json_t = nlohmann::ordered_json;

public:
  int run(int argc, char *argv[]) {
    parse_args(argc, argv);
    init_graph();
    generate_samples();
    run_test();
    generate_report();
    return tested_count_ != passed_count_;
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
  std::size_t passed_count_ = 0;
  std::size_t tested_count_ = 0;
  json_t failed_tests_ = json_t::array();

  void parse_args(int argc, char *argv[]) {
    CLI::App app("XPGraph query dependencies correctness test");
    app.add_option("--sample-size", sample_size_, "Number of sampled packages to test")
       ->required()->check(CLI::PositiveNumber);
    app.add_option("--max-depth", max_depth_, "Maximum dependency query depth")
       ->required()->check(CLI::PositiveNumber);
    app.add_option("--data-directory", data_directory_, "XPGraph data directory")
       ->required()->check(CLI::ExistingDirectory);
    app.add_option("--repository-config", repository_config_, "Loaded repository config file")
       ->required()->check(CLI::ExistingFile);
    app.add_option("--output", output_, "Correctness result JSON path")
       ->default_val("query_dependencies_correctness_test_report.json");
    auto *test_memgraph =
      app.add_flag("--test-memgraph", test_memgraph_, "Compare against an existing Memgraph instance");
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

  void run_test() {
    println("=== XPGraph Query Dependencies Correctness Test ===");
    println("Testing {} sample packages with depth from 1 to {}...", sample_size_, max_depth_);
    auto total_count = sample_size_ * max_depth_ * (test_memgraph_ ? 3 : 1);
    for (auto depth : std::views::iota(1ull, max_depth_ + 1)) {
      for (const auto &name : samples_) {
        auto cpu = std::get<DependencyFlat>(xpgraph_.query_dependencies(name, "", "", depth, false, false));
        check_backend(cpu, xpgraph_, name, depth, true, "gpu");
        if (test_memgraph_) {
          check_backend(cpu, mgxpgraph_, name, depth, false, "memgraph");
          check_backend(cpu, mgxpgraph_, name, depth, true, "memgraph_query_module");
        }
        if (tested_count_ % 100 == 0)
          println("Progress: {}/{} tests completed. Passed: {}, Failed: {}.",
                  tested_count_, total_count, passed_count_, tested_count_ - passed_count_);
      }
    }
    println("All tests completed. Total: {}, Passed: {}, Failed: {}.",
            tested_count_, passed_count_, tested_count_ - passed_count_);
    println("=========================================================");
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
    report["title"] = "XPGraph Query Dependencies Correctness Test";
    report["time"] = now_iso8601();
    report["repositories"] = repositories;
    auto &data = report["data"];
    data["package_count"] = xpgraph_.package_count();
    data["version_count"] = xpgraph_.version_count();
    data["dependency_count"] = xpgraph_.dependency_count();
    report["test_memgraph"] = test_memgraph_;
    report["sample_size"] = samples_.size();
    report["samples"] = samples_;
    report["max_depth"] = max_depth_;
    report["satisfy_architecture"] = true;
    report["satisfy_version"] = true;
    report["expand_alternative"] = true;
    report["total_test_count"] = tested_count_;
    report["passed_test_count"] = passed_count_;
    report["failed_test_count"] = tested_count_ - passed_count_;
    report["failed_tests"] = std::move(failed_tests_);
    auto serialized = report.dump(2);
    if (output_.empty()) {
      println("{}", serialized);
      return;
    }
    try {
      auto parent = output_.parent_path();
      if (!parent.empty()) std::filesystem::create_directories(parent);
      std::ofstream(output_) << serialized;
      println("Generated test report: {}", output_.string());
    } catch (const std::exception &error) {
      println("{}", serialized);
      println(std::cerr, "Failed to generate test report: {}", error.what());
    }
  }

  static bool check_result(const DependencyFlat &expected, const DependencyFlat &actual, std::string_view key,
                           json_t &report) {
    if (expected.size() != actual.size()) {
      report["reason"] = std::format("Dependency level count mismatch between cpu and {}.", key);
      report["cpu_level_count"] = expected.size();
      report[std::format("{}_level_count", key)] = actual.size();
      return false;
    }
    for (auto level : std::views::iota(0ull, expected.size())) {
      report["failed_level"] = level + 1;
      const auto &expected_level = expected[level];
      const auto &actual_level = actual[level];
      std::unordered_set expected_single(
        expected_level.single_dependencies.begin(), expected_level.single_dependencies.end());
      std::unordered_set actual_single(
        actual_level.single_dependencies.begin(), actual_level.single_dependencies.end());
      if (expected_single != actual_single) {
        report["reason"] = std::format("Single dependencies mismatch between cpu and {}.", key);
        report["cpu_single_dependency_count"] = expected_single.size();
        report[std::format("{}_single_dependency_count", key)] = actual_single.size();
        return false;
      }
      std::vector<std::unordered_set<DependencyInfo>> expected_groups, actual_groups;
      for (const auto &group : expected_level.alternative_dependencies)
        expected_groups.emplace_back(group.begin(), group.end());
      for (const auto &group : actual_level.alternative_dependencies)
        actual_groups.emplace_back(group.begin(), group.end());
      if (expected_groups.size() != actual_groups.size()) {
        report["reason"] = std::format("Alternative dependency group count mismatch between cpu and {}.", key);
        report["cpu_alternative_dependency_group_count"] = expected_groups.size();
        report[std::format("{}_alternative_dependency_group_count", key)] = actual_groups.size();
        return false;
      }
      for (const auto &expected_group : expected_groups) {
        bool found = false;
        for (const auto &actual_group : actual_groups)
          if (actual_group == expected_group) {
            found = true;
            break;
          }
        if (!found) {
          report["reason"] = std::format("Alternative dependency groups mismatch between cpu and {}.", key);
          return false;
        }
      }
    }
    return true;
  }

  void check_backend(const DependencyFlat &expected, GraphBase &graph, std::string_view name, std::size_t depth,
                     bool use_accelerator, std::string_view key) {
    json_t failure;
    failure["package_name"] = name;
    failure["query_depth"] = depth;
    failure["query_format"] = "flat";
    failure["comparison"] = std::format("cpu_vs_{}", key);
    auto actual = std::get<DependencyFlat>(
      graph.query_dependencies(name, "", "", depth, false, use_accelerator, true, true, true));
    if (check_result(expected, actual, key, failure)) { ++passed_count_; } else {
      println("Flat {} test failed for package: {}, depth={}.", key, name, depth);
      failed_tests_.emplace_back(std::move(failure));
    }
    ++tested_count_;
    if (auto *mgxpgraph = dynamic_cast<MGXPGraph *>(&graph)) mgxpgraph->clear_arena();
  }
};

} // namespace xpg

int main(int argc, char *argv[]) {
  try { return xpg::QueryDependenciesCorrectnessTest().run(argc, argv); } catch (const std::exception &error) {
    xpg::println(std::cerr, "Correctness test failed: {}", error.what());
    return 1;
  }
}
