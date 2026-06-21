#pragma once
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include "config.hpp"
#include "data_model.hpp"
#include "mmap_vector.hpp"
#include "string_pool.hpp"
#include "symbol_table.hpp"

namespace xpg {

class StorageGraph {
  friend class BufferGraph;
  friend class CacheGraph;

public:
  StorageGraph(std::size_t growth_bytes = kDefaultGrowthBytes) noexcept;
  StorageGraph(const std::filesystem::path &directory, open_mode mode = open_mode::kLoadOrCreate,
               std::initializer_list<std::string_view> architectures = {},
               std::initializer_list<std::string_view> dependency_types = {},
               std::size_t growth_bytes = kDefaultGrowthBytes) noexcept;
  StorageGraph(const StorageGraph &) = delete;
  StorageGraph &operator=(const StorageGraph &) = delete;
  StorageGraph(StorageGraph &&) noexcept = default;
  StorageGraph &operator=(StorageGraph &&) noexcept = default;
  ~StorageGraph() = default;

  void load(const std::filesystem::path &directory);
  void create(const std::filesystem::path &directory, std::initializer_list<std::string_view> architectures = {},
              std::initializer_list<std::string_view> dependency_types = {});
  void open(const std::filesystem::path &directory, open_mode mode = open_mode::kLoadOrCreate,
            std::initializer_list<std::string_view> architectures = {},
            std::initializer_list<std::string_view> dependency_types = {});
  void close();
  void sync();
  bool is_open() const noexcept { return control_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  std::size_t growth_bytes() const noexcept { return package_nodes_.growth_bytes(); }
  void set_growth_bytes(std::size_t growth_bytes) noexcept;
  std::size_t package_count() const noexcept { return package_nodes_.size(); }
  std::size_t version_count() const noexcept { return version_nodes_.size(); }
  std::size_t dependency_count() const noexcept { return dependency_edges_.size(); }
  bool empty() const noexcept { return package_nodes_.empty(); }

  const auto &architectures() const noexcept { return architectures_; }
  const auto &dependency_types() const noexcept { return dependency_types_; }
  ArchitectureId intern_architecture(std::string_view architecture);
  DependencyType intern_dependency_type(std::string_view dependency_type);

  PackageView get_package(PackageId pid) const noexcept;
  VersionView get_version(VersionId vid) const noexcept;
  DependencyView get_dependency(DependencyId did) const noexcept;
  std::optional<PackageView> get_package(std::string_view name) const noexcept;

  bool create_package(const PackageInfo &info, bool update_if_exists = false);
  bool delete_package(std::string_view name, std::string_view version, std::string_view architecture);

  void compact();

  std::vector<std::string_view> query_packages(std::string_view architecture, std::string_view prefix) const;
  std::vector<VersionInfo> query_versions(std::string_view name, std::string_view architecture) const;

  std::variant<DependencyTree, DependencyFlat> query_dependencies(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree) const;
  DependencyTree query_dependency_tree(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth) const;
  DependencyFlat query_dependency_flat(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth) const;

  static HOST_DEVICE bool match_architecture(ArchitectureId target, ArchitectureId constraint) noexcept {
    if (constraint == kAnyArchitecture || constraint == kAllArchitecture) return true;
    return target == kAllArchitecture || target == constraint;
  }

private:
  struct PackageNode {
    StringOffset name;
    VersionRangeId version_range;
  };

  struct VersionRange {
    VersionCount version_count;
    VersionId version_begin;
    VersionRangeId next;
  };

  struct VersionNode {
    StringOffset version;
    ArchitectureId architecture;
    DependencyCount dependency_count;
    DependencyId dependency_begin;
  };

  struct DependencyEdge {
    VersionId from_version;
    PackageId to_package;
    StringOffset version_constraint;
    ArchitectureId architecture_constraint;
    DependencyType type;
    GroupId group;
  };

  struct Control {
    std::size_t magic;
    std::size_t architecture_count;
    std::size_t dependency_type_count;
    std::size_t package_count;
    std::size_t version_range_count;
    std::size_t version_count;
    std::size_t dependency_count;
    std::size_t string_pool_size;
  };

  mmap_vector<std::byte> control_;
  symbol_table<ArchitectureId, StringLength> architectures_;
  symbol_table<DependencyType, StringLength> dependency_types_;
  mmap_vector<PackageNode> package_nodes_;
  mmap_vector<VersionRange> version_ranges_;
  mmap_vector<VersionNode> version_nodes_;
  mmap_vector<DependencyEdge> dependency_edges_;
  string_pool<StringLength> string_pool_;
  pooled_string_map<PackageId, StringLength> name_to_package_id_;
  constexpr static auto kVersionRangeEnd = static_cast<VersionRangeId>(-1);
  constexpr static std::size_t kMagic = 0x48505247474b5058; // "XPKGGRPH"

  Control &control() noexcept { return *reinterpret_cast<Control *>(control_.data()); }
  const Control &control() const noexcept { return *reinterpret_cast<const Control *>(control_.data()); }

  template <class F>
  void for_each_version(const PackageNode &pnode, F &&f) {
    for (auto vrid = pnode.version_range; vrid != kVersionRangeEnd;) {
      const auto &[version_count, version_begin, next] = version_ranges_[vrid];
      for (auto vid = version_begin; vid < version_begin + version_count; ++vid)
        if constexpr (std::invocable<F &&, VersionId, VersionNode &>)
          std::invoke(std::forward<F>(f), vid, version_nodes_[vid]);
        else if constexpr (std::invocable<F &&, VersionNode &, VersionId>)
          std::invoke(std::forward<F>(f), version_nodes_[vid], vid);
        else if constexpr (std::invocable<F &&, VersionId>)
          std::invoke(std::forward<F>(f), vid);
        else if constexpr (std::invocable<F &&, VersionNode &>)
          std::invoke(std::forward<F>(f), version_nodes_[vid]);
      vrid = next;
    }
  }

  template <class F>
  void for_each_version(const PackageNode &pnode, F &&f) const {
    for (auto vrid = pnode.version_range; vrid != kVersionRangeEnd;) {
      const auto &[version_count, version_begin, next] = version_ranges_[vrid];
      for (auto vid = version_begin; vid < version_begin + version_count; ++vid)
        if constexpr (std::invocable<F &&, VersionId, const VersionNode &>)
          std::invoke(std::forward<F>(f), vid, version_nodes_[vid]);
        else if constexpr (std::invocable<F &&, const VersionNode &, VersionId>)
          std::invoke(std::forward<F>(f), version_nodes_[vid], vid);
        else if constexpr (std::invocable<F &&, VersionId>)
          std::invoke(std::forward<F>(f), vid);
        else if constexpr (std::invocable<F &&, const VersionNode &>)
          std::invoke(std::forward<F>(f), version_nodes_[vid]);
      vrid = next;
    }
  }

  template <class F>
  void for_each_version(PackageId pid, F &&f) const { for_each_version(package_nodes_[pid], std::forward<F>(f)); }

  template <class F>
  void for_each_dependency(const VersionNode &vnode, F &&f) {
    for (auto did = vnode.dependency_begin; did != vnode.dependency_begin + vnode.dependency_count; ++did)
      if constexpr (std::invocable<F &&, DependencyId, DependencyEdge &>)
        std::invoke(std::forward<F>(f), did, dependency_edges_[did]);
      else if constexpr (std::invocable<F &&, DependencyEdge &, DependencyId>)
        std::invoke(std::forward<F>(f), dependency_edges_[did], did);
      else if constexpr (std::invocable<F &&, DependencyId>)
        std::invoke(std::forward<F>(f), did);
      else if constexpr (std::invocable<F &&, DependencyEdge &>)
        std::invoke(std::forward<F>(f), dependency_edges_[did]);
  }

  template <class F>
  void for_each_dependency(const VersionNode &vnode, F &&f) const {
    for (auto did = vnode.dependency_begin; did != vnode.dependency_begin + vnode.dependency_count; ++did)
      if constexpr (std::invocable<F &&, DependencyId, const DependencyEdge &>)
        std::invoke(std::forward<F>(f), did, dependency_edges_[did]);
      else if constexpr (std::invocable<F &&, const DependencyEdge &, DependencyId>)
        std::invoke(std::forward<F>(f), dependency_edges_[did], did);
      else if constexpr (std::invocable<F &&, DependencyId>)
        std::invoke(std::forward<F>(f), did);
      else if constexpr (std::invocable<F &&, const DependencyEdge &>)
        std::invoke(std::forward<F>(f), dependency_edges_[did]);
  }

  template <class F>
  void for_each_dependency(VersionId vid, F &&f) const { for_each_dependency(version_nodes_[vid], std::forward<F>(f)); }

  std::pair<PackageId, bool> create_package_node(std::string_view name);
  std::pair<VersionId, bool> create_version_node(PackageId pid, std::string_view version, ArchitectureId arch,
                                                 bool update_if_exists = false);
  std::pair<DependencyId, bool> create_dependency_edge(VersionId from_vid, PackageId to_pid, std::string_view vcons,
                                                       ArchitectureId acons, DependencyType type, GroupId group);

  void attach_versions(PackageId pid, VersionId vbegin, VersionCount vcount);
  void attach_dependencies(VersionId vid, DependencyId dbegin, DependencyCount dcount);

  std::vector<VersionId> init_frontier(std::string_view name, std::string_view version,
                                       std::string_view architecture) const;
};

} // namespace xpg
