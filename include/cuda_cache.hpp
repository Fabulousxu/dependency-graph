#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>
#include "config.hpp"
#include "types.hpp"

namespace xpg {

class CudaCache {
public:
  using VisitedMark = std::uint32_t;
  using TreeId = std::uint32_t;

  struct PackageNode {
    VersionId version_begin;
    VersionCount version_count;
  };

  struct VersionNode {
    StringId version_id;
    StringLength version_length;
    ArchitectureType architecture;
    DependencyCount dependency_count;
    DependencyId dependency_begin;
  };

  struct DependencyEdge {
    DependencyId original;
    PackageId to_package;
    StringId version_constraint_id;
    StringLength version_constraint_length;
    ArchitectureType architecture_constraint;
    DependencyType type;
    DependencyGroupId group;
  };

  CudaCache(const XPGraph &graph) noexcept;
  CudaCache(const CudaCache &) = delete;
  CudaCache &operator=(const CudaCache &) = delete;
  CudaCache(CudaCache &&other) noexcept;
  CudaCache &operator=(CudaCache &&other) noexcept;
  ~CudaCache() { clear(); }

  void build();
  void clear();
  bool is_built() const noexcept { return frontier_ != nullptr; }

  std::variant<DependencyTree, DependencyFlat> query_dependencies(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree,
    bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const;
  DependencyTree query_dependency_tree(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
    bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const;
  DependencyFlat query_dependency_flat(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
    bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const;

private:
  const XPGraph &graph_;
  std::vector<VersionId> to_cache_version_id_;
  PackageNode *package_nodes_;
  VersionNode *version_nodes_;
  DependencyEdge *dependency_edges_;
  char *string_pool_;
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

  struct BuildTreeNode {
    DependencyTree value;
    std::vector<TreeId> single_dependencies;
    std::vector<std::vector<TreeId>> alternative_dependencies;
  };

  std::vector<VersionId> init_frontier(std::string_view name, std::string_view version, ArchitectureType arch) const;
};

} // namespace xpg
