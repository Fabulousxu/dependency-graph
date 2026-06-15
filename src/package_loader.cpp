#include "package_loader.hpp"
#include <chrono>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include "util.hpp"

namespace xpg {

constexpr std::string_view trim(std::string_view sv) noexcept {
  auto first = sv.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string_view::npos) return sv.substr(sv.size());
  auto last = sv.find_last_not_of(" \t\n\r\f\v");
  return sv.substr(first, last - first + 1);
}

std::pair<PackageInfo, bool> PackageLoader::parse_deb_package(std::string_view raw_package) const noexcept {
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
  for (auto type : graph_.dependency_types()) {
    it = fields.find(type);
    if (it == fields.end()) continue;
    for (auto group : it->second | std::views::split(',')) {
      auto items = group | std::views::split('|');
      if (std::ranges::distance(items) > 1) {
        result.alternative_dependencies.emplace_back();
        for (auto item : items) {
          auto [info, succ] = parse_deb_dependency({item.begin(), item.end()}, type, result.architecture);
          if (succ) result.alternative_dependencies.back().emplace_back(std::move(info));
        }
      } else {
        auto [info, succ] = parse_deb_dependency({group.begin(), group.end()}, type, result.architecture);
        if (succ) result.single_dependencies.emplace_back(std::move(info));
      }
    }
  }
  return {result, true};
}

std::pair<DependencyInfo, bool> PackageLoader::parse_deb_dependency(
  std::string_view raw_dependency, std::string_view type, std::string_view architecture) const noexcept {
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

bool PackageLoader::load_deb_packages_file(const std::filesystem::path &path, bool flush_if_needed, bool verbose) {
  std::ifstream file(path);
  if (!file.good()) {
    if (verbose) println(std::cerr, "Failed to open DEB packages file: {}.", path.string());
    return false;
  }
  auto pcount = graph_.buffer_package_count();
  auto vcount = graph_.buffer_version_count();
  auto dcount = graph_.buffer_dependency_count();
  if (verbose) print("Loading packages file: {}... ", path.string());
  auto load_time = measure_time<std::chrono::milliseconds>([&, this] {
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
  if (verbose) println("Done. ({} ms)", load_time.count());
  pcount = graph_.buffer_package_count() - pcount;
  vcount = graph_.buffer_version_count() - vcount;
  dcount = graph_.buffer_dependency_count() - dcount;
  if (flush_if_needed) {
    auto memory_usage = graph_.estimated_memory_usage();
    if (memory_usage >= graph_.memory_limit()) {
      if (verbose)
        print("Estimated memory usage {:.1f} MiB exceeded limit {} MiB. Flushing to disk... ",
              memory_usage / MiBd, graph_.memory_limit() / MiB);
      auto flush_time = measure_time<std::chrono::milliseconds>([this] { graph_.flush_buffer(); });
      if (verbose) println("Done. ({:.3f} s)", flush_time.count() / 1000.0);
    }
  }
  if (verbose)
    println(
      "Loaded {} packages, {} versions, {} dependencies.\n"
      "Total {} packages, {} versions, {} dependencies in buffer; {} packages, {} versions, {} dependencies in disk.",
      pcount, vcount, dcount, graph_.buffer_package_count(), graph_.buffer_version_count(),
      graph_.buffer_dependency_count(), graph_.package_count(), graph_.version_count(), graph_.dependency_count());
  return true;
}

} // namespace xpg
