#pragma once
#include "dependency_graph.hpp"

class PackageLoader {
public:
  PackageLoader(DependencyGraph &graph) : graph_(graph) {}
  ~PackageLoader() = default;

  bool load_from_file(const std::string &filename, bool verbose = false) const;
  bool load_from_dataset_file(const std::string &dataset_filename, bool verbose = false) const;

private:
  struct DependencyItem {
    std::string_view package_name;
    std::string_view version_constraint;
    ArchitectureType architecture_constraint;
    GroupId group;
  };

  DependencyGraph &graph_;

  DependencyItem parse_dependency_(std::string_view raw_dep, GroupId group) const;
  std::vector<DependencyItem> parse_dependencies_(std::string_view raw_deps, GroupId &group) const;
  void load_raw_packages_(std::string_view raw_pkgs) const;
};
