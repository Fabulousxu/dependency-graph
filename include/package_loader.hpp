#pragma once
#include <filesystem>
#include <string_view>
#include <vector>
#include "dependency_graph.hpp"

class PackageLoader {
public:
  PackageLoader(DependencyGraph &graph) : graph_(graph) {}
  ~PackageLoader() = default;

  void load_package(std::string_view raw_package) const noexcept;
  void load_packages(std::string_view raw_packages) const noexcept;

  bool load_packages_file(const std::filesystem::path &path, bool verbose = false) const noexcept;
  bool load_dataset_file(const std::filesystem::path &path, bool verbose = false) const noexcept;

private:
  DependencyGraph &graph_;

  struct DependencyItem {
    std::string_view package_name;
    std::string_view version_constraint;
    ArchitectureType architecture_constraint;
    GroupId group;
  };

  DependencyItem parse_dependency(std::string_view raw_dep, GroupId group) const noexcept;
  std::vector<DependencyItem> parse_dependencies(std::string_view raw_deps, GroupId &group) const noexcept;

};
