#pragma once
#include <cstdint>
#include <vector>
#include "config.hpp"
#include "data_model.hpp"

class CacheGraph {
public:
  using VersionCount = std::uint16_t;
  using DependencyCount = std::uint16_t;
  using VisitedMark = std::uint16_t;

  struct PackageNode;
  struct VersionNode;
  struct DependencyEdge;

  CacheGraph(const StorageGraph &storage_graph) noexcept;

  CacheGraph(const CacheGraph &) = delete;
  CacheGraph &operator=(const CacheGraph &) = delete;

  CacheGraph(CacheGraph &&) noexcept = default;
  CacheGraph &operator=(CacheGraph &&) noexcept = delete;

  ~CacheGraph() { free_gpu(); }

  void build_cache();
  void free_gpu();

  DependencyResult query_dependencies(std::vector<VersionId> &frontier, std::size_t depth) const;

private:
  const StorageGraph &storage_graph_;
  std::vector<VersionId> to_cache_version_id_;
  mutable VisitedMark mark_;
  PackageNode *d_package_nodes_;
  VersionNode *d_version_nodes_;
  DependencyEdge *d_dependency_edges_;
  mutable VersionId *d_frontier_;
  mutable VersionId *d_next_;
  std::size_t *d_next_size_;
  DependencyId *d_dependency_ids_;
  std::size_t *d_dependency_count_;
  VisitedMark *d_visited_;

public:
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
    DependencyId original_id;
    PackageId to_package;
    ArchitectureId architecture_constraint;
    DependencyTypeId dependency_type;
    GroupId group;
  };

private:
  void init_gpu(const std::vector<PackageNode> &pnodes, const std::vector<VersionNode> &vnodes,
                const std::vector<DependencyEdge> &dedges);

  DependencyLevel expand_frontier(std::size_t &frontier_size, bool first_level, bool has_next) const;
};
