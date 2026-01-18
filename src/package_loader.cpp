#include "package_loader.hpp"
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include "dependency_graph.hpp"

using PL = PackageLoader;

bool PL::load_from_file(const std::string &filename, bool verbose) const {
  std::ifstream file{filename};
  if (!file) {
    if (verbose) std::cerr << std::format("Failed to open package file: {}.\n", filename);
    return false;
  }
  std::chrono::time_point<std::chrono::high_resolution_clock> start;
  if (verbose) {
    std::cout << std::format("Loading packages from file: {}... ", filename);
    start = std::chrono::high_resolution_clock::now();
  }
  file.seekg(0, std::ios::end);
  std::size_t file_size = file.tellg();
  file.seekg(0);
  std::string raw_pkgs;
  raw_pkgs.resize(file_size);
  file.read(raw_pkgs.data(), file_size);
  load_raw_packages(raw_pkgs);
  if (verbose) {
    auto end = std::chrono::high_resolution_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << std::format("Done. ({} ms)\n", time.count());
    std::cout << std::format("Loaded {} packages, {} versions, {} dependencies.\n",
      graph_.package_count(), graph_.version_count(), graph_.dependency_count());
  }
  return true;
}

bool PL::load_from_dataset_file(const std::string &dataset_filename, bool verbose) const {
  std::ifstream dfile{dataset_filename};
  if (!dfile) {
    if (verbose) std::cerr << std::format("Failed to open dataset file: {}.\n", dataset_filename);
    return false;
  }
  std::chrono::time_point<std::chrono::high_resolution_clock> begin;
  if (verbose) begin = std::chrono::high_resolution_clock::now();
  std::vector<std::string> to_load;
  std::string line;
  while (std::getline(dfile, line)) {
    if (line.empty()) continue;
    nlohmann::json item;
    std::istringstream{line} >> item;
    to_load.emplace_back(std::move(item["path"]));
  }
  if (verbose) std::cout << std::format("Loading {} package files...\n", to_load.size());
  std::size_t loaded_files = 0;
  for (const auto &filename : to_load) loaded_files += load_from_file(filename, verbose);
  if (verbose) {
    auto end = std::chrono::high_resolution_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
    std::cout << std::format("Loaded {} package files. ({} s)\n", loaded_files, time.count() / 1000.0);
  }
  return true;
}

std::string_view trim(std::string_view sv) {
  auto first = sv.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string_view::npos) return {};
  auto last = sv.find_last_not_of(" \t\n\r\f\v");
  return sv.substr(first, last - first + 1);
}

struct PL::DependencyItem {
  std::string_view name;
  std::string_view version_constraint;
  ArchitectureType architecture_constraint;
  GroupId group;
};

PL::DependencyItem PL::parse_dependency(std::string_view raw_dep, GroupId group) const {
  std::string_view version;
  auto lpar = raw_dep.find('(');
  if (lpar != std::string_view::npos)
    if (auto rpar = raw_dep.rfind(')'); rpar != std::string_view::npos && rpar > lpar)
      version = trim(raw_dep.substr(lpar + 1, rpar - lpar - 1));
  auto name_and_arch = raw_dep.substr(0, lpar);
  auto colon = name_and_arch.find(':');
  auto arch = colon != std::string_view::npos ? trim(name_and_arch.substr(colon + 1)) : "native";
  return {trim(name_and_arch.substr(0, colon)), version, graph_.architectures.insert(arch), group};
}

std::vector<PL::DependencyItem> PL::parse_dependencies(std::string_view raw_deps, GroupId &group) const {
  std::vector<DependencyItem> res;
  for (auto &&and_ : raw_deps | std::views::split(','))
    if (auto &&or_s = and_ | std::views::split('|'); std::ranges::distance(or_s) > 1) {
      for (auto &&or_ : or_s) res.emplace_back(parse_dependency({or_.begin(), or_.end()}, group));
      group++;
    } else res.emplace_back(parse_dependency({and_.begin(), and_.end()}, 0));
  return res;
}

void PL::load_raw_packages(std::string_view raw_pkgs) const {
  for (auto &&raw_pkg : raw_pkgs | std::views::split(std::string_view("\n\n"))) {
    if (trim({raw_pkg.begin(), raw_pkg.end()}).empty()) continue;
    std::unordered_map<std::string_view, std::string_view> kv;
    for (auto &&line : raw_pkg | std::views::split('\n')) {
      std::string_view line_view = {line.begin(), line.end()};
      if (line_view.empty()) continue;
      if (auto pos = line_view.find(':'); pos != std::string_view::npos)
        kv[trim(line_view.substr(0, pos))] = trim(line_view.substr(pos + 1));
    }

    const auto &pnode = graph_.create_package(kv.at("Package"));
    ArchitectureType arch = graph_.architectures.insert(kv.at("Architecture"));
    const auto &vnode = graph_.create_version(pnode, kv.at("Version"), arch);
    GroupId group = 1;
    for (const auto &dtype : graph_.dependency_types.symbols())
      if (auto it = kv.find(dtype); it != kv.end()) {
        auto dtid = graph_.dependency_types.id(dtype).value();
        for (auto &&item : parse_dependencies(it->second, group)) {
          const auto &dpnode = graph_.create_package(item.name);
          graph_.create_dependency(vnode, dpnode, item.version_constraint, item.architecture_constraint, dtid,
            item.group);
        }
      }
  }
}
