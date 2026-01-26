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
  bool test_load;
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
  app.add_flag("--test-load", opt.test_load);
  app.add_option("--load-dir", opt.load_dir)->needs("--test-load")->check(CLI::ExistingDirectory);
  app.add_option("--trials", opt.trials)->required()->check(CLI::PositiveNumber);
  app.add_option("--max-depth", opt.max_depth)->required()->check(CLI::PositiveNumber);
  app.add_option("--memory-limit", opt.memory_limit)->required()->check(CLI::PositiveNumber);
  app.add_option("--output", opt.output_file);
  CLI11_PARSE(app, argc, argv);

  std::filesystem::create_directories("./temp");
  DependencyGraph inmem_graph(std::numeric_limits<std::size_t>::max());
  DependencyGraph immflush_graph(0);
  DependencyGraph memlimit_graph(opt.memory_limit * MiB);
  DependencyGraph load_graph;
  if (!inmem_graph.open("./temp/data/in-memory", kCreate)) {
    println("Failed to create DependencyGraph at directory: {}", "./temp/data/in-memory");
    return 1;
  }
  if (!immflush_graph.open("./temp/data/immediate-flush", kCreate)) {
    println("Failed to create DependencyGraph at directory: {}", "./temp/data/immediate-flush");
    return 1;
  }
  if (!memlimit_graph.open("./temp/data/memory-limit", kCreate)) {
    println("Failed to create DependencyGraph at directory: {}", "./temp/data/memory-limit");
    return 1;
  }
  if (opt.test_load) {
    if (!load_graph.open(opt.load_dir, kLoad)) {
      println("Failed to load DependencyGraph from directory: {}", opt.load_dir);
      return 1;
    }
  }

  PackageLoader inmem_loader(inmem_graph);
  PackageLoader immflush_loader(immflush_graph);
  PackageLoader memlimit_loader(memlimit_graph);
  if (!inmem_loader.load_dataset_file(opt.dataset_file, true)) return 1;
  if (!immflush_loader.load_dataset_file(opt.dataset_file, true)) return 1;
  if (!memlimit_loader.load_dataset_file(opt.dataset_file, true)) return 1;

  print("Flushing to disk... ");
  auto flush_time = measure_time<std::chrono::milliseconds>([&] {
    immflush_graph.flush_buffer();
    memlimit_graph.flush_buffer();
  });
  println("Done. ({:.3f} s)", flush_time.count() / 1000.0);
  println("Total {} packages, {} versions, {} dependencies.",
          inmem_graph.buffer_package_count(),
          inmem_graph.buffer_version_count(),
          inmem_graph.buffer_dependency_count());
  print("Syncing to GPU... ");
  auto sync_time = measure_time<std::chrono::milliseconds>([&] {
    immflush_graph.sync_gpu();
    // memlimit_graph.sync_gpu();
  });
  println("Done. ({:.3f} s)", sync_time.count() / 1000.0);

  std::vector<std::string_view> to_query;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<std::size_t> dist(0, memlimit_graph.package_count() - 1);
  while (to_query.size() < opt.trials) {
    auto pview = memlimit_graph.get_package(dist(gen));
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
  result["test_load"] = opt.test_load;
  result["trials"] = opt.trials;
  result["max_depth"] = opt.max_depth;
  result["memory_limit"] = std::format("{} MiB", opt.memory_limit);
  result["in_memory_results"] = nlohmann::ordered_json::array();
  result["gpu_results"] = nlohmann::ordered_json::array();
  result["immediate_flush_results"] = nlohmann::ordered_json::array();
  result["memory_limit_results"] = nlohmann::ordered_json::array();
  if (opt.test_load) result["load_results"] = nlohmann::ordered_json::array();

  std::vector<std::vector<std::size_t>> inmem_times(opt.max_depth), gpu_times(opt.max_depth),
                                        immflush_times(opt.max_depth), memlimit_times(opt.max_depth),
                                        load_times(opt.max_depth);
  for (auto depth = 1; depth <= opt.max_depth; ++depth) {
    println("Testing depth={}...", depth);
    for (const auto &name : to_query) {
      auto [_, time] = measure_time<std::chrono::microseconds>([&inmem_graph, &name, depth] {
        return inmem_graph.query_dependencies_on_buffer(name, "", "", depth);
      });
      inmem_times[depth - 1].emplace_back(time.count());
    }
    auto &inmem_result = result["in_memory_results"].emplace_back();
    inmem_result["depth"] = depth;
    println("In-memory      tests completed. Average {:.3f} ms per query.",
            analyze_times(inmem_result, inmem_times[depth - 1], opt.trials));

    for (const auto &name : to_query) {
      auto [_, time] = measure_time<std::chrono::microseconds>([&immflush_graph, &name, depth] {
        return immflush_graph.query_dependencies(name, "", "", depth, true);
      });
      gpu_times[depth - 1].emplace_back(time.count());
    }
    auto &gpu_result = result["gpu_results"].emplace_back();
    gpu_result["depth"] = depth;
    println("GPU            tests completed. Average {:.3f} ms per query.",
            analyze_times(gpu_result, gpu_times[depth - 1], opt.trials));

    for (const auto &name : to_query) {
      auto [_, time] = measure_time<std::chrono::microseconds>([&immflush_graph, &name, depth] {
        return immflush_graph.query_dependencies(name, "", "", depth, false);
      });
      immflush_times[depth - 1].emplace_back(time.count());
    }
    auto &immflush_result = result["immediate_flush_results"].emplace_back();
    immflush_result["depth"] = depth;
    println("Imm-flush      tests completed. Average {:.3f} ms per query.",
            analyze_times(immflush_result, immflush_times[depth - 1], opt.trials));

    for (const auto &name : to_query) {
      auto [_, time] = measure_time<std::chrono::microseconds>([&memlimit_graph, &name, depth] {
        return memlimit_graph.query_dependencies(name, "", "", depth, false);
      });
      memlimit_times[depth - 1].emplace_back(time.count());
    }
    auto &memory_limit_result = result["memory_limit_results"].emplace_back();
    memory_limit_result["depth"] = depth;
    println("Memory-limited tests completed. Average {:.3f} ms per query.",
            analyze_times(memory_limit_result, memlimit_times[depth - 1], opt.trials));

    if (opt.test_load) {
      load_graph.open(opt.load_dir, kLoad);
      for (const auto &name : to_query) {
        auto [_, time] = measure_time<std::chrono::microseconds>([&load_graph, &name, depth] {
          return load_graph.query_dependencies(name, "", "", depth, false);
        });
        load_times[depth - 1].emplace_back(time.count());
      }
      auto &load_result = result["load_results"].emplace_back();
      load_result["depth"] = depth;
      println("Load           tests completed. Average {:.3f} ms per query.",
              analyze_times(load_result, load_times[depth - 1], opt.trials));
      load_graph.close();
    }
  }
  println("All tests completed.");
  println("====================================");

  println("Cleaning up...");
  inmem_graph.close();
  immflush_graph.close();
  memlimit_graph.close();
  load_graph.close();
  std::filesystem::remove_all("./temp");
  std::filesystem::create_directories(std::filesystem::path(opt.output_file).parent_path());
  std::ofstream(opt.output_file) << result.dump(2);
  return 0;
}
