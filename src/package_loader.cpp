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

std::string_view trim(std::string_view sv) {
  auto first = sv.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string_view::npos) return {};
  auto last = sv.find_last_not_of(" \t\n\r\f\v");
  return sv.substr(first, last - first + 1);
}

PackageLoader::DependencyItem PackageLoader::parse_dependency_(std::string_view raw_dep, GroupId group) const {
  DependencyItem item;
  item.group = group;
  auto lpar = raw_dep.find('(');
  if (lpar != std::string_view::npos) {
    auto rpar = raw_dep.rfind(')');
    if (rpar != std::string_view::npos && rpar > lpar)
      item.version_constraint = trim(raw_dep.substr(lpar + 1, rpar - lpar - 1));
  }
  auto name_and_arch = raw_dep.substr(0, lpar);
  auto colon = name_and_arch.find(':');
  item.package_name = trim(name_and_arch.substr(0, colon));
  auto arch = colon != std::string_view::npos ? trim(name_and_arch.substr(colon + 1)) : "native";
  item.architecture_constraint = graph_.architectures().add(arch);
  return item;
}

std::vector<PackageLoader::DependencyItem> PackageLoader::parse_dependencies_(std::string_view raw_deps,
  GroupId &group) const {
  std::vector<DependencyItem> result;
  for (auto and_ : raw_deps | std::views::split(','))
    if (auto or_s = and_ | std::views::split('|'); std::ranges::distance(or_s) > 1) {
      for (auto or_ : or_s) result.emplace_back(parse_dependency_({or_.begin(), or_.end()}, group));
      group++;
    } else result.emplace_back(parse_dependency_({and_.begin(), and_.end()}, 0));
  return result;
}

void PackageLoader::load_raw_packages_(std::string_view raw_pkgs) const {
  for (auto raw_pkg : raw_pkgs | std::views::split(std::string_view("\n\n"))) {
    if (trim({raw_pkg.begin(), raw_pkg.end()}).empty()) continue;
    std::unordered_map<std::string_view, std::string_view> kv;
    for (auto line : raw_pkg | std::views::split('\n')) {
      std::string_view line_view = {line.begin(), line.end()};
      if (line_view.empty()) continue;
      if (auto pos = line_view.find(':'); pos != std::string_view::npos)
        kv[trim(line_view.substr(0, pos))] = trim(line_view.substr(pos + 1));
    }

    auto [pid, psucc] = graph_.create_package(kv.at("Package"));
    ArchitectureType arch = graph_.architectures().add(kv.at("Architecture"));
    auto [vid, vsucc] = graph_.create_version(pid, kv.at("Version"), arch);
    GroupId group = 1;

    for (DependencyType dtid = 0; dtid < graph_.dependency_types().size(); ++dtid) {
      auto dtype = graph_.dependency_types().get(dtid);
      if (auto it = kv.find(dtype); it != kv.end())
        for (auto item : parse_dependencies_(it->second, group)) {
          auto [dpid, dpsucc] = graph_.create_package(item.package_name);
          graph_.create_dependency(vid, dpid, item.version_constraint, item.architecture_constraint, dtid, item.group);
        }
    }
  }
}

bool PackageLoader::load_from_file(const std::string &filename, bool verbose) const {
  std::ifstream file{filename};
  if (!file) {
    if (verbose) std::cerr << std::format("Failed to open package file: {}.\n", filename);
    return false;
  }
  std::size_t pcount, vcount, dcount;
  std::chrono::time_point<std::chrono::high_resolution_clock> start;
  if (verbose) {
    std::cout << std::format("Loading packages from file: {}... ", filename);
    pcount = graph_.buffer_package_count();
    vcount = graph_.buffer_version_count();
    dcount = graph_.buffer_dependency_count();
    start = std::chrono::high_resolution_clock::now();
  }
  file.seekg(0, std::ios::end);
  std::size_t file_size = file.tellg();
  file.seekg(0);
  std::string raw_pkgs;
  raw_pkgs.resize(file_size);
  file.read(raw_pkgs.data(), file_size);
  load_raw_packages_(raw_pkgs);
  if (verbose) {
    auto end = std::chrono::high_resolution_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << std::format("Done. ({} ms)\n", time.count());
    pcount = graph_.buffer_package_count() - pcount;
    vcount = graph_.buffer_version_count() - vcount;
    dcount = graph_.buffer_dependency_count() - dcount;
  }

  auto memory_usage = graph_.estimated_memory_usage();
  if (memory_usage >= graph_.memory_limit()) {
    if (verbose) {
      start = std::chrono::high_resolution_clock::now();
      std::cout << std::format("Estimated memory usage {:.1f} MB exceeded threshold {} MB. Flushing to disk... ",
        memory_usage / 1024 / 1024.0, graph_.memory_limit() / 1024 / 1024);
    }
    graph_.flush_to_disk();
    if (verbose) {
      auto end = std::chrono::high_resolution_clock::now();
      auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
      std::cout << std::format("Done. ({} ms)\n", time.count());
    }
  }
  if (verbose) {
    std::cout << std::format(
      "Loaded {} packages, {} versions, {} dependencies. Total {} packages, {} versions, {} dependencies.\n",
      pcount, vcount, dcount,
      graph_.package_count() + graph_.buffer_package_count(),
      graph_.version_count() + graph_.buffer_version_count(),
      graph_.dependency_count() + graph_.buffer_dependency_count()
    );
  }
  return true;
}

bool PackageLoader::load_from_dataset_file(const std::string &dataset_filename, bool verbose) const {
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
