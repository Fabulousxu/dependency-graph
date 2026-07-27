#pragma once
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>
#include "config.hpp"
#include "cuda_cache.hpp"
#include "json_serialization.hpp"
#include "mmap_vector.hpp"
#include "package_loader.hpp"
#include "string_pool.hpp"
#include "symbol_table.hpp"
#include "types.hpp"
#include "write_buffer.hpp"

namespace xpg {

class XPGraph : public GraphBase {
  friend class WriteBuffer;
  friend class CudaCache;

  struct PackageNode {
    StringId name;
    VersionRangeId version_range;
  };

  struct VersionRange {
    VersionCount version_count;
    VersionId version_begin;
    VersionRangeId next;
  };

  struct VersionNode {
    StringId version;
    ArchitectureType architecture;
    DependencyCount dependency_count;
    DependencyId dependency_begin;
  };

  struct DependencyEdge {
    VersionId from_version;
    PackageId to_package;
    StringId version_constraint;
    ArchitectureType architecture_constraint;
    DependencyType type;
    DependencyGroupId group;
  };

  struct Meta {
    std::size_t magic;
    std::size_t architecture_types;
    std::size_t dependency_types;
    std::size_t package_count;
    std::size_t version_ranges;
    std::size_t version_count;
    std::size_t dependency_count;
    std::size_t string_pool_size;
  };

public:
  XPGraph(std::size_t flush_limit_bytes = kFlushLimitBytes, std::size_t growth_bytes = kGrowthBytes) noexcept;
  XPGraph(const std::filesystem::path &directory, open_mode mode = open_mode::kLoadOrCreate,
          std::span<const std::string_view> architecture_types = kArchitectureTypes,
          std::span<const std::string_view> dependency_types = kDependencyTypes,
          std::size_t flush_limit_bytes = kFlushLimitBytes, std::size_t growth_bytes = kGrowthBytes);
  XPGraph(const std::filesystem::path &directory, open_mode mode,
          std::initializer_list<std::string_view> architecture_types,
          std::initializer_list<std::string_view> dependency_types,
          std::size_t flush_limit_bytes = kFlushLimitBytes, std::size_t growth_bytes = kGrowthBytes);
  XPGraph(const XPGraph &) = delete;
  XPGraph &operator=(const XPGraph &) = delete;
  XPGraph(XPGraph &&) noexcept = default;
  XPGraph &operator=(XPGraph &&) noexcept = default;
  ~XPGraph() override { XPGraph::close(); }

  void load(const std::filesystem::path &directory);
  void create(const std::filesystem::path &directory,
              std::span<const std::string_view> architecture_types = kArchitectureTypes,
              std::span<const std::string_view> dependency_types = kDependencyTypes);
  void create(const std::filesystem::path &directory,
              std::initializer_list<std::string_view> architecture_types,
              std::initializer_list<std::string_view> dependency_types);
  void open(const std::filesystem::path &directory, open_mode mode = open_mode::kLoadOrCreate,
            std::span<const std::string_view> architecture_types = kArchitectureTypes,
            std::span<const std::string_view> dependency_types = kDependencyTypes);
  void open(const std::filesystem::path &directory, open_mode mode,
            std::initializer_list<std::string_view> architecture_types,
            std::initializer_list<std::string_view> dependency_types);
  void sync();
  void close() override;
  bool is_open() const noexcept override { return meta_.is_open(); }

  std::size_t growth_bytes() const noexcept { return package_nodes_.growth_bytes(); }
  void set_growth_bytes(std::size_t growth_bytes) noexcept;
  std::size_t flush_limit_bytes() const noexcept { return flush_limit_bytes_; }
  void set_flush_limit_bytes(std::size_t flush_limit_bytes) noexcept { flush_limit_bytes_ = flush_limit_bytes; }
  std::size_t estimated_memory_usage() const noexcept;

  void flush_buffer(bool update_if_exists = false) { write_buffer_.flush(update_if_exists); }
  bool flush_buffer_if_needed(bool update_if_exists = false);
  void clear_buffer() noexcept { write_buffer_.clear(); }
  bool buffer_empty() const noexcept { return write_buffer_.empty(); }
  void build_cache() const { cuda_cache_.build(); }
  void clear_cache() const { cuda_cache_.clear(); }
  bool cache_built() const { return cuda_cache_.is_built(); }
  void compact();

  std::size_t package_count() const noexcept override { return package_nodes_.size(); }
  std::size_t version_count() const noexcept override { return version_nodes_.size(); }
  std::size_t dependency_count() const noexcept override { return dependency_edges_.size(); }
  bool empty() const noexcept override { return package_nodes_.empty(); }

  std::size_t package_count_in_buffer() const noexcept { return write_buffer_.package_count(); }
  std::size_t version_count_in_buffer() const noexcept { return write_buffer_.version_count(); }
  std::size_t dependency_count_in_buffer() const noexcept { return write_buffer_.dependency_count(); }

  const symbol_table<ArchitectureType, StringLength> &architecture_types() const noexcept;
  const symbol_table<DependencyType, StringLength> &dependency_types() const noexcept;
  ArchitectureType intern_architecture(std::string_view architecture);
  DependencyType intern_dependency(std::string_view dependency);

  PackageView get_package(PackageId pid) const noexcept override;
  std::optional<PackageView> get_package(std::string_view name) const noexcept override;
  bool create_package(const PackageInfo &info, bool update_if_exists = false) override;
  bool delete_package(std::string_view name, std::string_view version,
                      std::string_view architecture) override;

  PackageView get_package_in_buffer(PackageId pid) const noexcept;
  std::optional<PackageView> get_package_in_buffer(std::string_view name) const noexcept;
  bool create_package_in_buffer(const PackageInfo &info, bool update_if_exists = false);

  std::vector<std::string_view> query_packages(std::string_view architecture = "",
                                               std::string_view prefix = "") const override;
  std::vector<VersionInfo> query_versions(std::string_view name, std::string_view architecture = "") const override;
  std::variant<DependencyTree, DependencyFlat> query_dependencies(
    std::string_view name, std::string_view version = "", std::string_view architecture = "", std::size_t depth = 1,
    bool tree = true, bool use_gpu = false, bool satisfy_architecture = true, bool satisfy_version = true,
    bool expand_alternative = true) const override;
  std::variant<DependencyTree, DependencyFlat> query_dependencies_in_buffer(
    std::string_view name, std::string_view version = "", std::string_view architecture = "", std::size_t depth = 1,
    bool tree = true, bool satisfy_architecture = true, bool satisfy_version = true,
    bool expand_alternative = true) const;

  DependencyTree query_dependency_tree(
    std::string_view name, std::string_view version = "", std::string_view architecture = "", std::size_t depth = 1,
    bool satisfy_architecture = true, bool satisfy_version = true, bool expand_alternative = true) const;
  DependencyFlat query_dependency_flat(
    std::string_view name, std::string_view version = "", std::string_view architecture = "", std::size_t depth = 1,
    bool satisfy_architecture = true, bool satisfy_version = true, bool expand_alternative = true) const;

private:
  mmap_vector<std::byte> meta_;
  symbol_table<ArchitectureType, StringLength> architecture_types_;
  symbol_table<DependencyType, StringLength> dependency_types_;
  mmap_vector<PackageNode> package_nodes_;
  mmap_vector<VersionRange> version_ranges_;
  mmap_vector<VersionNode> version_nodes_;
  mmap_vector<DependencyEdge> dependency_edges_;
  string_pool<StringLength> string_pool_;
  pooled_string_map<PackageId, StringLength> name_to_package_id_;
  WriteBuffer write_buffer_;
  mutable CudaCache cuda_cache_;
  std::size_t flush_limit_bytes_;
  constexpr static VersionRangeId kVersionRangeEnd = -1u;
  constexpr static std::size_t kMagic = 0x4850415247505858; // "XXPGRAPH"

  Meta &meta() noexcept { return *reinterpret_cast<Meta *>(meta_.data()); }
  const Meta &meta() const noexcept { return *reinterpret_cast<const Meta *>(meta_.data()); }

  VersionView get_version(VersionId vid) const noexcept override;
  DependencyView get_dependency(DependencyId did) const noexcept override;

  std::pair<PackageId, bool> create_package_node(std::string_view name);
  std::pair<VersionId, bool> create_version_node(PackageId pid, std::string_view version, ArchitectureType arch,
                                                 bool update_if_exists = false);
  std::pair<DependencyId, bool> create_dependency_edge(VersionId from_vid, PackageId to_pid, std::string_view vcons,
                                                       ArchitectureType acons, DependencyType type,
                                                       DependencyGroupId group);

  void attach_versions(PackageId pid, VersionId vbegin, VersionCount vcount);
  void attach_dependencies(VersionId vid, DependencyId dbegin, DependencyCount dcount);

  std::vector<VersionId> init_frontier(std::string_view name, std::string_view version, ArchitectureType arch) const;

  template <class F>
  void for_each_version(const PackageNode &pnode, F &&f) {
    for (auto vrid = pnode.version_range; vrid != kVersionRangeEnd;) {
      const auto &vrange = version_ranges_[vrid];
      for (auto vid = vrange.version_begin; vid < vrange.version_begin + vrange.version_count; ++vid)
        if constexpr (std::invocable<F &&, VersionId, VersionNode &>)
          std::invoke(std::forward<F>(f), vid, version_nodes_[vid]);
        else if constexpr (std::invocable<F &&, VersionNode &, VersionId>)
          std::invoke(std::forward<F>(f), version_nodes_[vid], vid);
        else if constexpr (std::invocable<F &&, VersionId>)
          std::invoke(std::forward<F>(f), vid);
        else if constexpr (std::invocable<F &&, VersionNode &>)
          std::invoke(std::forward<F>(f), version_nodes_[vid]);
      vrid = vrange.next;
    }
  }

  template <class F>
  void for_each_version(const PackageNode &pnode, F &&f) const {
    for (auto vrid = pnode.version_range; vrid != kVersionRangeEnd;) {
      const auto &vrange = version_ranges_[vrid];
      for (auto vid = vrange.version_begin; vid < vrange.version_begin + vrange.version_count; ++vid)
        if constexpr (std::invocable<F &&, VersionId, const VersionNode &>)
          std::invoke(std::forward<F>(f), vid, version_nodes_[vid]);
        else if constexpr (std::invocable<F &&, const VersionNode &, VersionId>)
          std::invoke(std::forward<F>(f), version_nodes_[vid], vid);
        else if constexpr (std::invocable<F &&, VersionId>)
          std::invoke(std::forward<F>(f), vid);
        else if constexpr (std::invocable<F &&, const VersionNode &>)
          std::invoke(std::forward<F>(f), version_nodes_[vid]);
      vrid = vrange.next;
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
};

} // namespace xpg
