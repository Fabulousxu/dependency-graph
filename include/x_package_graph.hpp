#pragma once
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>
#include "buffer_graph.hpp"
#include "cache_graph.hpp"
#include "config.hpp"
#include "data_model.hpp"
#include "storage_graph.hpp"

namespace xpg {

class XPackageGraph {
public:
  XPackageGraph(std::size_t memory_limit = -1ull, std::size_t growth_bytes = kDefaultGrowthBytes) noexcept;
  XPackageGraph(const std::filesystem::path &directory, open_mode mode = open_mode::kLoadOrCreate,
                std::size_t memory_limit = -1ull, std::size_t growth_bytes = kDefaultGrowthBytes);
  XPackageGraph(const XPackageGraph &) = delete;
  XPackageGraph &operator=(const XPackageGraph &) = delete;
  XPackageGraph(XPackageGraph &&) noexcept = default;
  XPackageGraph &operator=(XPackageGraph &&) = delete;
  ~XPackageGraph() { close(); }

  void load(const std::filesystem::path &directory) { storage_graph_.load(directory); }
  void create(const std::filesystem::path &directory);
  void open(const std::filesystem::path &directory, open_mode mode = open_mode::kLoadOrCreate);
  void sync() { storage_graph_.sync(); }
  void close();
  bool is_open() const noexcept { return storage_graph_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  std::size_t growth_bytes() const noexcept { return storage_graph_.growth_bytes(); }
  void set_growth_bytes(std::size_t growth_bytes) noexcept { storage_graph_.set_growth_bytes(growth_bytes); }
  std::size_t memory_limit() const noexcept { return memory_limit_; }
  void set_memory_limit(std::size_t memory_limit) noexcept { memory_limit_ = memory_limit; }
  std::size_t package_count() const noexcept { return storage_graph_.package_count(); }
  std::size_t version_count() const noexcept { return storage_graph_.version_count(); }
  std::size_t dependency_count() const noexcept { return storage_graph_.dependency_count(); }

  const auto &architectures() const noexcept { return storage_graph_.architectures(); }
  const auto &dependency_types() const noexcept { return storage_graph_.dependency_types(); }
  ArchitectureId intern_architecture(std::string_view architecture);
  DependencyType intern_dependency_type(std::string_view dependency_type);

  PackageView get_package(PackageId pid) const noexcept { return storage_graph_.get_package(pid); }
  std::optional<PackageView> get_package(std::string_view name) const noexcept;

  bool create_package(const PackageInfo &info) { return buffer_graph_.create_package(info, false); }
  bool delete_package(std::string_view name, std::string_view version, std::string_view architecture);

  std::size_t estimated_memory_usage() const noexcept;
  void flush_buffer(bool update_if_exists = false) { buffer_graph_.flush(update_if_exists); }
  bool flush_buffer_if_needed(bool update_if_exists = false);
  void build_cache() const { cache_graph_.build(); }
  void clear_cache() const { cache_graph_.clear(); }
  void compact() { storage_graph_.compact(); }

  std::vector<std::string_view> query_packages(std::string_view architecture = "", std::string_view prefix = "") const;
  std::vector<VersionInfo> query_versions(std::string_view name, std::string_view architecture = "") const;
  std::variant<DependencyTree, DependencyFlat> query_dependencies(
    std::string_view name, std::string_view version = "", std::string_view architecture = "", std::size_t depth = 1,
    bool tree = true, bool use_gpu = false) const;

  // Only use for test
  std::size_t buffer_package_count() const noexcept { return buffer_graph_.package_count(); }
  std::size_t buffer_version_count() const noexcept { return buffer_graph_.version_count(); }
  std::size_t buffer_dependency_count() const noexcept { return buffer_graph_.dependency_count(); }
  PackageView get_package_in_buffer(PackageId pid) const noexcept { return buffer_graph_.get_package(pid); }
  std::optional<PackageView> get_package_in_buffer(std::string_view name) const
    noexcept { return buffer_graph_.get_package(name); }
  std::variant<DependencyTree, DependencyFlat> query_dependencies_in_buffer(
    std::string_view name, std::string_view version = "", std::string_view architecture = "", std::size_t depth = 1,
    bool tree = true) const { return buffer_graph_.query_dependencies(name, version, architecture, depth, tree); }

private:
  StorageGraph storage_graph_;
  BufferGraph buffer_graph_;
  mutable CacheGraph cache_graph_;
  std::size_t memory_limit_;

  static constexpr std::initializer_list<std::string_view> kDefaultArchitectures = {"", "any", "all"};
  static constexpr std::initializer_list<std::string_view> kDefaultDependencyTypes = {
    "DEPENDS", "PRE_DEPENDS", "RECOMMENDS", "SUGGESTS", "BREAKS", "CONFLICTS", "PROVIDES", "REPLACES", "ENHANCES",
    "BUILT_USING", "STATIC_BUILT_USING", "JAVASCRIPT_BUILT_USING", "X_CARGO_BUILT_USING"
  };
};

} // namespace xpg
