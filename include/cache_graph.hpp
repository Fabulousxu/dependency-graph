#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>
#include "config.hpp"
#include "data_model.hpp"

namespace xpg {

using cuda_size_t = unsigned long long;
inline constexpr std::size_t kMaxDeviceVectorBytes = 64 * MiB;
template <class T> inline constexpr std::size_t kMaxDeviceVectorSize = kMaxDeviceVectorBytes / sizeof(T);

class CacheGraph {
public:
  using VisitedMark = std::uint32_t;
  using TreeId = std::uint32_t;

  struct PackageNode {
    VersionId version_begin;
    VersionCount version_count;
  };

  struct VersionNode {
    ArchitectureId architecture;
    DependencyCount dependency_count;
    DependencyId dependency_begin;
  };

  struct DependencyEdge {
    DependencyId original;
    PackageId to_package;
    ArchitectureId architecture_constraint;
    DependencyType type;
    GroupId group;
  };

  CacheGraph(const StorageGraph &storage_graph) noexcept;
  CacheGraph(const CacheGraph &) = delete;
  CacheGraph &operator=(const CacheGraph &) = delete;
  CacheGraph(CacheGraph &&) noexcept = default;
  CacheGraph &operator=(CacheGraph &&) noexcept = delete;
  ~CacheGraph() { clear(); }

  void build();
  void clear();
  bool is_built() const noexcept { return frontier_ != nullptr; }

  std::variant<DependencyTree, DependencyFlat> query_dependencies(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree) const;
  DependencyTree query_dependency_tree(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth) const;
  DependencyFlat query_dependency_flat(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth) const;

private:
  const StorageGraph &storage_graph_;
  std::vector<VersionId> to_cache_version_id_;
  PackageNode *package_nodes_;
  VersionNode *version_nodes_;
  DependencyEdge *dependency_edges_;
  mutable VersionId *frontier_;
  mutable TreeId *frontier_trees_;
  mutable VersionId *next_;
  mutable TreeId *next_trees_;
  cuda_size_t *next_size_;
  DependencyId *result_;
  TreeId *result_trees_;
  cuda_size_t *result_size_;
  VisitedMark *visited_;
  mutable VisitedMark mark_;

  std::vector<VersionId> init_frontier(std::string_view name, std::string_view version,
                                       std::string_view architecture) const;
};

} // namespace xpg
