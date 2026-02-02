#include "package_loader.hpp"
#include <chrono>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include "dependency_graph.hpp"
#include "util.hpp"

DependencyInfo PackageLoader::parse_dependency(std::string_view raw_dep, std::string_view dtype) noexcept {
  DependencyInfo info{.dependency_type = dtype};
  auto lpar = raw_dep.find('(');
  if (lpar != std::string_view::npos) {
    auto rpar = raw_dep.rfind(')');
    if (rpar != std::string_view::npos && rpar > lpar)
      info.version_constraint = trim(raw_dep.substr(lpar + 1, rpar - lpar - 1));
  }
  auto name_and_arch = raw_dep.substr(0, lpar);
  auto colon = name_and_arch.find(':');
  info.package_name = trim(name_and_arch.substr(0, colon));
  info.architecture_constraint = colon != std::string_view::npos ? trim(name_and_arch.substr(colon + 1)) : "native";
  return info;
}

void PackageLoader::parse_dependencies(std::string_view raw_deps, std::string_view dtype, PackageInfo &info) noexcept {
  for (auto and_ : raw_deps | std::views::split(',')) {
    auto or_s = and_ | std::views::split('|');
    if (std::ranges::distance(or_s) > 1) {
      info.or_dependencies.emplace_back();
      for (auto or_ : or_s) {
        std::string_view raw_dep(or_.begin(), or_.end());
        info.or_dependencies.back().emplace_back(parse_dependency(raw_dep, dtype));
      }
    } else {
      std::string_view raw_dep(and_.begin(), and_.end());
      info.direct_dependencies.emplace_back(parse_dependency(raw_dep, dtype));
    }
  }
}

void PackageLoader::load_package(std::string_view raw_package) const noexcept {
  if (trim(raw_package).empty()) return;
  std::unordered_map<std::string_view, std::string_view> fields;
  for (auto line : raw_package | std::views::split('\n')) {
    if (line.empty()) continue;
    std::string_view lview(line.begin(), line.end());
    auto colon = lview.find(':');
    if (colon != std::string_view::npos)
      fields.emplace(trim(lview.substr(0, colon)), trim(lview.substr(colon + 1)));
  }
  PackageInfo info;
  auto nit = fields.find("Package");
  if (nit == fields.end()) return;
  info.name = nit->second;
  auto ait = fields.find("Architecture");
  if (ait == fields.end()) return;
  info.architecture = ait->second;
  auto vit = fields.find("Version");
  if (vit == fields.end()) return;
  info.version = vit->second;
  for (auto dtype : graph_.dependency_types()) {
    auto it = fields.find(dtype);
    if (it == fields.end()) continue;
    parse_dependencies(it->second, dtype, info);
  }
  graph_.create_package(info);
}

void PackageLoader::load_packages(std::string_view raw_packages) const noexcept {
  for (auto raw_package : raw_packages | std::views::split(std::string_view("\n\n"))) {
    std::string_view pview(raw_package.begin(), raw_package.end());
    load_package(pview);
  }
}

bool PackageLoader::load_packages_file(const std::filesystem::path &path, bool verbose) const noexcept {
  std::ifstream file(path);
  if (!file.good()) {
    if (verbose) println(std::cerr, "Failed to open packages file: {}.", path.string());
    return false;
  }
  auto pcount = graph_.buffer_package_count();
  auto vcount = graph_.buffer_version_count();
  auto dcount = graph_.buffer_dependency_count();
  if (verbose) print("Loading packages file: {}... ", path.string());
  auto load_time = measure_time<std::chrono::milliseconds>([this, &file] {
    file.seekg(0, std::ios::end);
    auto file_size = file.tellg();
    file.seekg(0);
    std::string raw_pkgs(file_size, '\0');
    file.read(raw_pkgs.data(), file_size);
    load_packages(raw_pkgs);
  });
  if (verbose) println("Done. ({} ms)", load_time.count());
  pcount = graph_.buffer_package_count() - pcount;
  vcount = graph_.buffer_version_count() - vcount;
  dcount = graph_.buffer_dependency_count() - dcount;

  auto memory_usage = graph_.estimated_memory_usage();
  if (memory_usage >= graph_.memory_limit()) {
    if (verbose)
      print("Estimated memory usage {:.1f} MiB exceeded limit {} MiB. Flushing to disk... ",
            memory_usage / MiBd, graph_.memory_limit() / MiB);
    auto flush_time = measure_time<std::chrono::milliseconds>([this] { graph_.flush_buffer(); });
    if (verbose) println("Done. ({:.3f} s)", flush_time.count() / 1000.0);
  }
  if (verbose)
    println("Loaded {} packages, {} versions, {} dependencies. Total {} packages, {} versions, {} dependencies.",
            pcount, vcount, dcount, graph_.package_count(), graph_.version_count(), graph_.dependency_count());
  return true;
}

bool PackageLoader::load_dataset_file(const std::filesystem::path &path, bool verbose) const noexcept {
  std::ifstream file(path);
  if (!file.good()) {
    if (verbose) println(std::cerr, "Failed to open dataset file: {}.", path.string());
    return false;
  }
  std::vector<std::string> to_load;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) continue;
    if (!nlohmann::json::accept(line)) continue;
    auto item = nlohmann::json::parse(line);
    auto it = item.find("path");
    if (it != item.end()) to_load.emplace_back(std::move(it.value()));
  }

  if (verbose) println("Loading {} packages files...", to_load.size());
  auto [load_count, load_time] = measure_time<std::chrono::milliseconds>([this, &to_load, verbose] {
    std::size_t count = 0;
    for (auto &filename : to_load) count += load_packages_file(std::move(filename), verbose);
    return count;
  });
  if (verbose) println("Loaded {} packages files. ({:.3f} s)", load_count, load_time.count() / 1000.0);
  return true;
}
