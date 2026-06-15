#pragma once
#include <filesystem>
#include <string_view>
#include <utility>
#include "x_package_graph.hpp"

namespace xpg {

class PackageLoader {
public:
  PackageLoader(XPackageGraph &graph) noexcept : graph_(graph) {}
  PackageLoader(const PackageLoader &) noexcept = default;
  PackageLoader &operator=(const PackageLoader &) = delete;
  PackageLoader(PackageLoader &&) noexcept = default;
  PackageLoader &operator=(PackageLoader &&) = delete;
  ~PackageLoader() = default;

  std::pair<PackageInfo, bool> parse_deb_package(std::string_view raw_package) const noexcept;
  std::pair<DependencyInfo, bool> parse_deb_dependency(std::string_view raw_dependency, std::string_view type,
                                                       std::string_view architecture) const noexcept;

  bool load_deb_packages_file(const std::filesystem::path &path, bool flush_if_needed = true, bool verbose = false);

private:
  XPackageGraph &graph_;
};

} // namespace xpg
