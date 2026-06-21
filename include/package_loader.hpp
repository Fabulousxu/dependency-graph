#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "data_model.hpp"
#include "x_package_graph.hpp"

namespace xpg {

struct RepositoryInfo {
  bool enabled = true;
  std::vector<std::string> url;
  std::vector<std::string> distributions;
  std::vector<std::string> architectures;
};

using RepositoryConfig = std::unordered_map<std::string, RepositoryInfo>;

class PackageLoader {
public:
  PackageLoader(XPackageGraph &graph) noexcept : graph_(graph) {}
  PackageLoader(const PackageLoader &) noexcept = default;
  PackageLoader &operator=(const PackageLoader &) = delete;
  PackageLoader(PackageLoader &&) noexcept = default;
  PackageLoader &operator=(PackageLoader &&) = delete;
  ~PackageLoader() = default;

  static std::pair<PackageInfo, bool> parse_deb_package(std::string_view raw_package);
  static std::pair<DependencyInfo, bool> parse_deb_dependency(std::string_view raw_dependency, std::string_view type,
                                                              std::string_view architecture);

  bool load_deb_packages(const std::filesystem::path &path, bool flush_if_needed = true, bool verbose = false) const;
  std::pair<std::size_t, std::size_t> load_repositories(const RepositoryConfig &config,
                                                        const std::filesystem::path &cache_directory = "",
                                                        bool flush_if_needed = true, bool verbose = false) const;
  std::pair<std::size_t, std::size_t> load_repositories(const std::filesystem::path &config_path,
                                                        const std::filesystem::path &cache_directory = "",
                                                        bool flush_if_needed = true, bool verbose = false) const;

  void flush_buffer(bool update_if_exists = false, bool verbose = false) const;
  void build_cache(bool verbose = false) const;
  void compact(bool verbose = false) const;

private:
  XPackageGraph &graph_;

  inline static const std::unordered_map<std::string_view, std::string_view> kDebDependencyTypeMap = {
    {"Depends", "DEPENDS"}, {"Pre-Depends", "PRE_DEPENDS"}, {"Recommends", "RECOMMENDS"}, {"Suggests", "SUGGESTS"},
    {"Breaks", "BREAKS"}, {"Conflicts", "CONFLICTS"}, {"Provides", "PROVIDES"}, {"Replaces", "REPLACES"},
    {"Enhances", "ENHANCES"}, {"Built-Using", "BUILT_USING"}, {"Static-Built-Using", "STATIC_BUILT_USING"},
    {"Javascript-Built-Using", "JAVASCRIPT_BUILT_USING"}, {"X-Cargo-Built-Using", "X_CARGO_BUILT_USING"},
  };
};

} // namespace xpg
