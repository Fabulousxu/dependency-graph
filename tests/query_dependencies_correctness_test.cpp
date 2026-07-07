#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
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
  CLI::App app("XPGraph query dependencies correctness test");
  app.add_option("--sample-size", option.sample_size, "Number of sampled packages to test")
     ->required()->check(CLI::PositiveNumber);
  app.add_option("--max-depth", option.max_depth, "Maximum dependency query depth")
     ->required()->check(CLI::PositiveNumber);
  app.add_option("--repository-config", option.repository_config, "Repository config file to load")
     ->required()->check(CLI::ExistingFile);
  app.add_option("--cache-directory", option.cache_directory, "Repository cache directory to load")
     ->required()->check(CLI::ExistingDirectory);
  app.add_option("--temp-directory", option.temp_directory, "Temporary data directory")->default_val("temp");
  app.add_option("--output", option.output, "Correctness result JSON path")
     ->default_val("query_dependencies_correctness_test_report.json");
  try { app.parse(argc, argv); } catch (const CLI::ParseError &exc) { std::exit(app.exit(exc)); }
  return option;
}

std::vector<std::string> generate_samples(const xpg::XPGraph &graph, std::size_t sample_size) {
  std::vector<std::string> samples;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<std::size_t> dist(0, graph.package_count() - 1);
  while (samples.size() < sample_size) {
    auto pview = graph.get_package(dist(gen));
    if (pview.versions().size() < 2) continue;
    samples.emplace_back(pview.name);
  }
  std::ranges::sort(samples);
  return samples;
}

template <class Json>
bool check_result(const xpg::DependencyFlat &cpu, const xpg::DependencyFlat &gpu, Json &j) {
  for (auto level : std::views::iota(0ull, cpu.size())) {
    j["failed_level"] = level + 1;
    const auto &clevel = cpu[level];
    const auto &glevel = gpu[level];
    std::unordered_set csingle(clevel.single_dependencies.begin(), clevel.single_dependencies.end());
    std::unordered_set gsingle(glevel.single_dependencies.begin(), glevel.single_dependencies.end());
    if (csingle != gsingle) {
      j["reason"] = "Single dependencies mismatch between cpu and gpu.";
      j["cpu_single_dependency_count"] = csingle.size();
      j["gpu_single_dependency_count"] = gsingle.size();
      return false;
    }
    std::vector<std::unordered_set<xpg::DependencyInfo>> cgroups, ggroups;
    for (const auto &cgroup : clevel.alternative_dependencies) cgroups.emplace_back(cgroup.begin(), cgroup.end());
    for (const auto &ggroup : glevel.alternative_dependencies) ggroups.emplace_back(ggroup.begin(), ggroup.end());
    if (cgroups.size() != ggroups.size()) {
      j["reason"] = "Alternative dependency group count mismatch between cpu and gpu.";
      j["cpu_alternative_dependency_group_count"] = cgroups.size();
      j["gpu_alternative_dependency_group_count"] = ggroups.size();
      return false;
    }
    for (const auto &cgroup : cgroups) {
      bool found = false;
      for (const auto &ggroup : ggroups)
        if (ggroup == cgroup) {
          found = true;
          break;
        }
      if (!found) {
        j["reason"] = "Alternative dependency groups mismatch between cpu and gpu.";
        return false;
      }
    }
  }
  return true;
}

int main(int argc, char *argv[]) {
  auto [sample_size, max_depth, repository_config, cache_directory, temp_directory, output] = parse_args(argc, argv);
  std::ifstream file(repository_config);
  if (!file.good())
    throw std::runtime_error(std::format("Failed to open repository config file: {}.", repository_config.string()));
  auto config = nlohmann::json::parse(file).get<xpg::RepositoryConfig>();
  xpg::XPGraph graph(temp_directory, xpg::open_mode::kCreate);
  xpg::PackageLoader loader(graph);
  nlohmann::ordered_json jreport, jfaileds = nlohmann::ordered_json::array();
  auto load_time = xpg::measure_time<std::chrono::milliseconds>(
    [&] { loader.load_repositories(repository_config, cache_directory, false, true); });
  auto flush_time = xpg::measure_time<std::chrono::milliseconds>([&] { loader.flush_buffer(false, true); });
  auto cache_time = xpg::measure_time<std::chrono::milliseconds>([&] { loader.build_cache(); });
  auto samples = generate_samples(graph, sample_size);

  xpg::println("=== XPGraph Query Dependencies Correctness Test ===");
  xpg::println("Testing {} sample packages with depth from 1 to {}...", sample_size, max_depth);
  std::size_t passed_count = 0, tested_count = 0;
  for (auto depth : std::views::iota(1ull, max_depth + 1)) {
    for (const auto &name : samples) {
      nlohmann::ordered_json jfailed;
      jfailed["package_name"] = name;
      jfailed["query_depth"] = depth;
      jfailed["query_format"] = "flat";
      auto cpu_flat = std::get<xpg::DependencyFlat>(graph.query_dependencies(name, "", "", depth, false, false));
      auto gpu_flat = std::get<xpg::DependencyFlat>(graph.query_dependencies(name, "", "", depth, false, true));
      if (check_result(cpu_flat, gpu_flat, jfailed)) ++passed_count;
      else {
        xpg::println("Flat test failed for package: {}, depth={}.", name, depth);
        jfaileds.emplace_back(std::move(jfailed));
      }
      ++tested_count;
      if (tested_count % 100 == 0)
        xpg::println("Progress: {}/{} tests completed. Passed: {}, Failed: {}.",
                     tested_count, sample_size * max_depth, passed_count, tested_count - passed_count);
    }
  }
  xpg::println("All tests completed. Total: {}, Passed: {}, Failed: {}.",
               tested_count, passed_count, tested_count - passed_count);
  xpg::println("=========================================================");

  jreport["title"] = "XPGraph Query Dependencies Correctness Test";
  jreport["time"] = xpg::now_iso8601();
  jreport["repositories"] = config;
  auto &jdata = jreport["data"];
  jdata["package_count"] = graph.package_count();
  jdata["version_count"] = graph.version_count();
  jdata["dependency_count"] = graph.dependency_count();
  jdata["load_seconds"] = load_time.count() / 1000.0;
  jdata["flush_seconds"] = flush_time.count() / 1000.0;
  jdata["cache_seconds"] = cache_time.count() / 1000.0;
  jreport["sample_size"] = samples.size();
  jreport["samples"] = samples;
  jreport["max_depth"] = max_depth;
  jreport["filter_architecture"] = true;
  jreport["filter_version"] = true;
  jreport["expand_alternative"] = true;
  jreport["total_test_count"] = tested_count;
  jreport["passed_test_count"] = passed_count;
  jreport["failed_test_count"] = tested_count - passed_count;
  jreport["failed_tests"] = std::move(jfaileds);
  if (!output.empty()) {
    auto report = jreport.dump(2);
    try {
      std::filesystem::create_directories(output.parent_path());
      std::ofstream(output) << report;
    } catch (const std::exception &e) {
      xpg::println("{}", report);
      xpg::println(std::cerr, "Failed to generate test report: {}", e.what());
    }
    xpg::println("Generated test report: {}", output.string());
  }
  xpg::println("Cleaning up...");
  graph.close();
  std::filesystem::remove_all(temp_directory);
  return tested_count - passed_count > 0;
}
