#include "package_loader.hpp"
#include <chrono>
#include <cstddef>
#include <cuda_runtime.h>
#include <format>
#include <fstream>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include "config.hpp"
#include "json_serialization.hpp"
#include "util.hpp"

namespace xpg {

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

constexpr std::string_view trim(std::string_view sv) noexcept {
  auto first = sv.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string_view::npos) return sv.substr(sv.size());
  auto last = sv.find_last_not_of(" \t\n\r\f\v");
  return sv.substr(first, last - first + 1);
}

std::pair<PackageInfo, bool> PackageLoader::parse_deb_package(std::string_view raw_package) {
  PackageInfo result;
  std::unordered_map<std::string_view, std::string_view> fields;
  for (auto line : raw_package | std::views::split('\n')) {
    if (line.empty()) continue;
    std::string_view lview(line.begin(), line.end());
    auto colon = lview.find(':');
    if (colon != std::string_view::npos) fields.emplace(trim(lview.substr(0, colon)), trim(lview.substr(colon + 1)));
  }
  auto it = fields.find("Package");
  if (it != fields.end()) { result.name = it->second; } else return {{}, false};
  it = fields.find("Architecture");
  if (it != fields.end()) { result.architecture = it->second; } else return {{}, false};
  it = fields.find("Version");
  if (it != fields.end()) { result.version = it->second; } else return {{}, false};
  for (const auto &[field_name, stored_type] : kDebDependencyTypeMap) {
    it = fields.find(field_name);
    if (it == fields.end()) continue;
    for (auto group : it->second | std::views::split(',')) {
      auto items = group | std::views::split('|');
      if (std::ranges::distance(items) > 1) {
        result.alternative_dependencies.emplace_back();
        for (auto item : items) {
          auto [info, succ] = parse_deb_dependency({item.begin(), item.end()}, stored_type, result.architecture);
          if (succ) result.alternative_dependencies.back().emplace_back(std::move(info));
        }
      } else {
        auto [info, succ] = parse_deb_dependency({group.begin(), group.end()}, stored_type, result.architecture);
        if (succ) result.single_dependencies.emplace_back(std::move(info));
      }
    }
  }
  return {result, true};
}

std::pair<DependencyInfo, bool> PackageLoader::parse_deb_dependency(
  std::string_view raw_dependency, std::string_view type, std::string_view architecture) {
  DependencyInfo info{.type = type};
  auto lpar = raw_dependency.find('(');
  if (lpar != std::string_view::npos) {
    auto rpar = raw_dependency.rfind(')');
    if (rpar != std::string_view::npos && rpar > lpar)
      info.version_constraint = trim(raw_dependency.substr(lpar + 1, rpar - lpar - 1));
    else return {{}, false};
  }
  raw_dependency = raw_dependency.substr(0, lpar);
  auto colon = raw_dependency.find(':');
  info.name = trim(raw_dependency.substr(0, colon));
  info.architecture = colon != std::string_view::npos ? trim(raw_dependency.substr(colon + 1)) : architecture;
  return {info, true};
}

bool PackageLoader::load_deb_packages(const std::filesystem::path &path, bool flush_if_needed, bool verbose) const {
  std::ifstream file(path, std::ios::binary);
  if (!file.good()) {
    if (verbose) println(std::cerr, "Failed to open DEB packages file: {}.", path.string());
    return false;
  }
  auto pcount = graph_.buffer_package_count();
  auto vcount = graph_.buffer_version_count();
  auto dcount = graph_.buffer_dependency_count();
  if (verbose) print("Loading DEB packages file: {}... ", path.string());
  auto time = measure_time<std::chrono::milliseconds>([&, this] {
    file.seekg(0, std::ios::end);
    auto file_size = file.tellg();
    file.seekg(0);
    std::string pkgs(file_size, '\0');
    file.read(pkgs.data(), file_size);
    for (auto pkg : pkgs | std::views::split(std::string_view("\n\n"))) {
      auto [info, succ] = parse_deb_package({pkg.begin(), pkg.end()});
      if (succ) graph_.create_package(info);
    }
  });
  if (verbose) println("Done. ({} ms)", time.count());
  pcount = graph_.buffer_package_count() - pcount;
  vcount = graph_.buffer_version_count() - vcount;
  dcount = graph_.buffer_dependency_count() - dcount;
  if (flush_if_needed) {
    auto memory_usage = graph_.estimated_memory_usage();
    if (memory_usage >= graph_.memory_limit()) {
      if (verbose) {
        println("  Estimated memory usage {:.1f} MiB exceeded limit {} MiB.",
                memory_usage / MiBd, graph_.memory_limit() / MiB);
        print("  Flushing to disk... ");
      }
      auto flush_time = measure_time<std::chrono::milliseconds>([this] { graph_.flush_buffer(); });
      if (verbose) println("Done. ({:.3f} s)", flush_time.count() / 1000.0);
    }
  }
  if (verbose) {
    println("  Loaded: + {} packages, + {} versions, + {} dependencies.", pcount, vcount, dcount);
    println("  Total: {} packages, {} versions, {} dependencies (buffer)",
            graph_.buffer_package_count(), graph_.buffer_version_count(), graph_.buffer_dependency_count());
    println("         {} packages, {} versions, {} dependencies (disk)",
            graph_.package_count(), graph_.version_count(), graph_.dependency_count());
  }
  return true;
}

std::pair<std::size_t, std::size_t> PackageLoader::load_repositories(const RepositoryConfig &config,
                                                                     const std::filesystem::path &cache_directory,
                                                                     bool flush_if_needed, bool verbose) const {
  std::size_t total = 0, loaded = 0;
  auto time = measure_time<std::chrono::milliseconds>([&, this] {
    for (const auto &[name, repo] : config) {
      if (!repo.enabled) {
        if (verbose) println("Skipping disabled repository: {}", name);
        continue;
      }
      for (const auto &dist : repo.distributions)
        for (const auto &arch : repo.architectures) {
          auto path = cache_directory / name / dist / arch;
          std::error_code ec;
          if (!std::filesystem::is_regular_file(path, ec)) {
            if (verbose) {
              println("Cached DEB packages file not found: {}.", path.string());
              println("  Downloading from {}/dists/{}/main/binary-{}/Packages.gz... ", repo.url[0], dist, arch);
              println(std::cerr, "  Network download is not supported yet. Skipping");
            }
            continue;
          }
          ++total;
          if (load_deb_packages(path, flush_if_needed, verbose)) ++loaded;
        }
    }
  });
  if (verbose) println("Loaded DEB repositories ({:.3f} s). {}/{} loaded", time.count() / 1000.0, loaded, total);
  return {total, loaded};
}

std::pair<std::size_t, std::size_t> PackageLoader::load_repositories(const std::filesystem::path &config_path,
                                                                     const std::filesystem::path &cache_directory,
                                                                     bool flush_if_needed, bool verbose) const {
  std::ifstream file(config_path);
  if (!file.good())
    throw std::runtime_error(std::format("Failed to open repository config file: {}", config_path.string()));
  auto config = nlohmann::json::parse(file).get<RepositoryConfig>();
  if (verbose) println("Loading packages from repository config file: {}... ", config_path.string());
  return load_repositories(config, cache_directory, flush_if_needed, verbose);
}

void PackageLoader::flush_buffer(bool update_if_exists, bool verbose) const {
  if (verbose) print("Flushing to disk... ");
  auto time = xpg::measure_time<std::chrono::milliseconds>([=, this] { graph_.flush_buffer(update_if_exists); });
  if (verbose) {
    println("Done. ({:.3f} s)", time.count() / 1000.0);
    println("  Total: {} packages, {} versions, {} dependencies (disk)",
            graph_.package_count(), graph_.version_count(), graph_.dependency_count());
  }
}

void PackageLoader::build_cache(bool verbose) const {
  if (verbose) print("Building cache on GPU... ");
  graph_.clear_cache();
  auto before = getGpuMemInfo();
  auto time = xpg::measure_time<std::chrono::milliseconds>([this] { graph_.build_cache(); });
  auto after = getGpuMemInfo();
  auto used = before.free_bytes - after.free_bytes;
  if (verbose) {
    println("Done. ({:.3f} s)", time.count() / 1000.0);
    println("  GPU memory: {:.3f} GiB used, {:.3f} GiB free, {:.3f} GiB total",
            used / GiBd, after.free_bytes / GiBd, after.total_bytes / GiBd);
  }
}

void PackageLoader::compact(bool verbose) const {
  if (verbose) print("Compact storage... ");
  auto time = xpg::measure_time<std::chrono::milliseconds>([this] { graph_.compact(); });
  if (verbose) {
    println("Done. ({:.3f} s)", time.count() / 1000.0);
    println("  Total: {} packages, {} versions, {} dependencies (disk)",
            graph_.package_count(), graph_.version_count(), graph_.dependency_count());
  }
}

} // namespace xpg
