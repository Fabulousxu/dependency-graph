#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
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

double analyze_times(std::vector<std::size_t> &times, std::size_t trials, nlohmann::ordered_json &result) {
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
  std::string load_dir;
  std::size_t trials;
  std::size_t max_depth;
  std::size_t memory_limit;
  std::string output_file;
};

int main(int argc, char *argv[]) {
  Option opt;
  CLI::App app;
  app.add_option("--dataset", opt.dataset_file)->required()->check(CLI::ExistingFile);
  app.add_option("--load-dir", opt.load_dir)->check(CLI::ExistingDirectory);
  app.add_option("--trials", opt.trials)->required()->check(CLI::PositiveNumber);
  app.add_option("--max-depth", opt.max_depth)->required()->check(CLI::PositiveNumber);
  app.add_option("--memory-limit", opt.memory_limit)->default_val(1024)->check(CLI::PositiveNumber);
  app.add_option("--output", opt.output_file);
  CLI11_PARSE(app, argc, argv);

  std::filesystem::create_directories("./temp");
  DependencyGraph inmem_graph(std::numeric_limits<std::size_t>::max());
  DependencyGraph test_graph(opt.memory_limit * MiB);
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
  print("Flushing to disk... ");
  auto flush_time = measure_time<std::chrono::milliseconds>([&] { test_graph.flush_buffer(); });
  println("Done. ({:.3f} s)", flush_time.count() / 1000.0);
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

  println("=== Query Dependencies Benchmark ===");
  println("Testing {} packages with max_depth={}, total {} tests...",
          opt.trials, opt.max_depth, opt.trials * opt.max_depth);
  nlohmann::ordered_json result;
  result["title"] = "Query Dependencies Benchmark";
  result["time"] = now_iso8601();
  result["package_count"] = inmem_graph.buffer_package_count();
  result["version_count"] = inmem_graph.buffer_version_count();
  result["dependency_count"] = inmem_graph.buffer_dependency_count();
  result["test_load"] = !opt.load_dir.empty();
  result["trials"] = opt.trials;
  result["max_depth"] = opt.max_depth;
  result["memory_limit"] = std::format("{} MiB", opt.memory_limit);
  result["in_memory_results"] = nlohmann::ordered_json::array();
  result["gpu_results"] = nlohmann::ordered_json::array();
  result["disk_results"] = nlohmann::ordered_json::array();
  if (!opt.load_dir.empty()) result["loaded_results"] = nlohmann::ordered_json::array();

  std::vector<std::vector<std::size_t>> inmem_times(opt.max_depth), gpu_times(opt.max_depth),
                                        disk_times(opt.max_depth), load_times(opt.max_depth);
  for (auto depth = 1; depth <= opt.max_depth; ++depth) {
    println("Testing depth={}...", depth);
    for (const auto &name : to_query) {
      auto [_, time] = measure_time<std::chrono::microseconds>([&] {
        return inmem_graph.query_dependencies_on_buffer(name, "", "", depth);
      });
      inmem_times[depth - 1].emplace_back(time.count());
    }
    auto &inmem_result = result["in_memory_results"].emplace_back();
    inmem_result["depth"] = depth;
    println("In-memory tests completed. Average {:.3f} ms per query.",
            analyze_times(inmem_times[depth - 1], opt.trials, inmem_result));

    for (const auto &name : to_query) {
      auto [_, time] = measure_time<std::chrono::microseconds>([&] {
        return test_graph.query_dependencies(name, "", "", depth, true);
      });
      gpu_times[depth - 1].emplace_back(time.count());
    }
    auto &gpu_result = result["gpu_results"].emplace_back();
    gpu_result["depth"] = depth;
    println("GPU       tests completed. Average {:.3f} ms per query.",
            analyze_times(gpu_times[depth - 1], opt.trials, gpu_result));

    for (const auto &name : to_query) {
      auto [_, time] = measure_time<std::chrono::microseconds>([&] {
        return test_graph.query_dependencies(name, "", "", depth, false);
      });
      disk_times[depth - 1].emplace_back(time.count());
    }
    auto &disk_result = result["disk_results"].emplace_back();
    disk_result["depth"] = depth;
    println("Disk      tests completed. Average {:.3f} ms per query.",
            analyze_times(disk_times[depth - 1], opt.trials, disk_result));

    if (!opt.load_dir.empty()) {
      load_graph.load(opt.load_dir);
      for (const auto &name : to_query) {
        auto [_, time] = measure_time<std::chrono::microseconds>([&] {
          return load_graph.query_dependencies(name, "", "", depth, false);
        });
        load_times[depth - 1].emplace_back(time.count());
      }
      auto &load_result = result["loaded_results"].emplace_back();
      load_result["depth"] = depth;
      println("Loaded    tests completed. Average {:.3f} ms per query.",
              analyze_times(load_times[depth - 1], opt.trials, load_result));
      load_graph.close();
    }
  }
  println("All tests completed.");
  println("====================================");

  std::filesystem::create_directories(std::filesystem::path(opt.output_file).parent_path());
  if (!opt.output_file.empty()) std::ofstream(opt.output_file) << result.dump(2);
  println("Cleaning up...");
  inmem_graph.close();
  test_graph.close();
  std::filesystem::remove_all("./temp");
  return 0;
}
