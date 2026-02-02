#pragma once
#include <filesystem>
#include <string_view>
#include "dependency_graph.hpp"

class PackageLoader {
public:
  PackageLoader(DependencyGraph &graph) : graph_(graph) {}

  PackageLoader(const PackageLoader &) = default;
  PackageLoader &operator=(const PackageLoader &) = delete;

  PackageLoader(PackageLoader &&) = default;
  PackageLoader &operator=(PackageLoader &&) = delete;

  ~PackageLoader() = default;

  void load_package(std::string_view raw_package) const noexcept;
  void load_packages(std::string_view raw_packages) const noexcept;

  bool load_packages_file(const std::filesystem::path &path, bool verbose = false) const noexcept;
  bool load_dataset_file(const std::filesystem::path &path, bool verbose = false) const noexcept;

private:
  DependencyGraph &graph_;

  static DependencyInfo parse_dependency(std::string_view raw_dep, std::string_view dtype) noexcept;
  static void parse_dependencies(std::string_view raw_deps, std::string_view dtype, PackageInfo &info) noexcept;
};
