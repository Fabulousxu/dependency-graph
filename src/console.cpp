#include <cuda_runtime.h>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include "package_loader.hpp"
#include "util.hpp"
#include "x_package_graph.hpp"

std::filesystem::path project_path = "/home/xupeigong/projects/deps-analyzer";
std::filesystem::path repos_path = project_path / "data/repos";
std::filesystem::path data_path = project_path / "data/xpg/test-console";

struct GpuMemInfo {
  std::size_t free_bytes;
  std::size_t total_bytes;
};

GpuMemInfo getGpuMemInfo() {
  GpuMemInfo result;
  auto code = cudaMemGetInfo(&result.free_bytes, &result.total_bytes);
  if (code != cudaSuccess) throw std::runtime_error(cudaGetErrorString(code));
  return result;
}

void flush_buffer_with_report(xpg::XPackageGraph &graph, bool verbose = false) {
  if (verbose) xpg::print("Flushing to disk... ");
  auto time = xpg::measure_time<std::chrono::milliseconds>([&] { graph.flush_buffer(); });
  if (verbose) xpg::println("Done. ({:.3f} s)", time.count() / 1000.0);
  if (verbose)
    xpg::println("Total {} packages, {} versions, {} dependencies.",
                 graph.package_count(), graph.version_count(), graph.dependency_count());
}

void build_cache_with_report(xpg::XPackageGraph &graph, bool verbose = false) {
  if (verbose) xpg::print("Building cache on GPU... ");
  graph.clear_cache();
  auto before = getGpuMemInfo();
  auto time = xpg::measure_time<std::chrono::milliseconds>([&] { graph.build_cache(); });
  if (verbose) xpg::println("Done. ({:.3f} s)", time.count() / 1000.0);
  auto after = getGpuMemInfo();
  auto used = before.free_bytes - after.free_bytes;
  if (verbose)
    xpg::println("GPU memory usage: {:.3f} GiB, {:.3f} GiB free, {:.3f} GiB total.",
                 used / xpg::GiBd, after.free_bytes / xpg::GiBd, after.total_bytes / xpg::GiBd);
}

void load_packages(xpg::XPackageGraph &graph, bool verbose = false) {
  while (true) {
    xpg::println("> Load packages (type ':q' to quit, ':f' to flush buffer, ':c' to build cache)");
    std::string path;
    xpg::print(">   path: ");
    std::getline(std::cin, path);
    if (path == ":q") {
      flush_buffer_with_report(graph, verbose);
      build_cache_with_report(graph, verbose);
      return;
    }
    if (path == ":f") {
      flush_buffer_with_report(graph, verbose);
      continue;
    }
    if (path == ":c") {
      build_cache_with_report(graph, verbose);
      continue;
    }
    xpg::PackageLoader(graph).load_deb_packages_file(path, true, verbose);
  }
}

void query_packages(const xpg::XPackageGraph &graph) {
  while (true) {
    xpg::println("> Query packages (type ':q' to quit)");
    std::string architecture, prefix, limit, offset;
    xpg::print(">   architecture (empty for any): ");
    std::getline(std::cin, architecture);
    if (architecture == ":q") return;
    xpg::print(">   prefix (empty for any): ");
    std::getline(std::cin, prefix);
    if (prefix == ":q") return;
    xpg::print(">   limit (default is 0): ");
    std::getline(std::cin, limit);
    if (limit == ":q") return;
    if (limit.empty()) limit = "0";
    xpg::print(">   offset (default is 0): ");
    std::getline(std::cin, offset);
    if (offset == ":q") return;
    if (offset.empty()) offset = "0";
    auto result = graph.query_packages_json(architecture, prefix, std::stoull(limit), std::stoull(offset));
    std::cout << result.dump(2) << std::endl;
  }
}

void query_versions(const xpg::XPackageGraph &graph) {
  while (true) {
    xpg::println("> Query versions (type ':q' to quit)");
    std::string name, architecture;
    xpg::print(">   name: ");
    std::getline(std::cin, name);
    if (name == ":q") return;
    xpg::print(">   architecture (empty for any): ");
    std::getline(std::cin, architecture);
    if (architecture == ":q") return;
    auto result = graph.query_versions_json(name, architecture);
    std::cout << result.dump(2) << std::endl;
  }
}

void query_dependencies(const xpg::XPackageGraph &graph) {
  while (true) {
    xpg::println("> Query dependencies (type ':q' to quit)");
    std::string name, version, architecture, depth, format, use_gpu;
    xpg::print(">   name: ");
    std::getline(std::cin, name);
    if (name == ":q") return;
    xpg::print(">   version (empty for any): ");
    std::getline(std::cin, version);
    if (version == ":q") return;
    xpg::print(">   architecture (empty for any): ");
    std::getline(std::cin, architecture);
    if (architecture == ":q") return;
    xpg::print(">   depth (default is 1): ");
    std::getline(std::cin, depth);
    if (depth == ":q") return;
    if (depth.empty()) depth = "1";
    xpg::print(">   format (t/f, tree or flat, default is t): ");
    std::getline(std::cin, format);
    if (format == ":q") return;
    if (format.empty()) format = "t";
    xpg::print(">   use GPU (y/n, default is n): ");
    std::getline(std::cin, use_gpu);
    if (use_gpu == ":q") return;
    if (use_gpu.empty()) use_gpu = "n";
    auto result =
      graph.query_dependencies_json(name, version, architecture, std::stoull(depth), format == "t", use_gpu == "y");
    std::cout << result.dump(2) << std::endl;
  }
}

int main() {
  xpg::XPackageGraph graph(data_path, xpg::open_mode::kCreate);
  while (true) {
    xpg::println("> Select mode");
    xpg::println(">   - 'l' to load packages");
    xpg::println(">   - 'p' to query packages");
    xpg::println(">   - 'v' to query versions");
    xpg::println(">   - 'd' to query dependencies");
    xpg::println(">   - 'q' to quit");
    xpg::print(">   mode: ");
    std::string mode;
    std::getline(std::cin, mode);
    if (mode == "q") break;
    if (mode == "l") load_packages(graph, true);
    else if (mode == "p") query_packages(graph);
    else if (mode == "v") query_versions(graph);
    else if (mode == "d") query_dependencies(graph);
  }
  graph.close();
  return 0;
}
