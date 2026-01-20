#pragma once
#include "config.hpp"
#include "result_model.hpp"

constexpr auto kMaxDeviceVectorSize = 16 * 1024 * 1024;

class GpuGraph {
  friend class DependencyGraph;

  using VersionCountType = std::uint16_t;
  using DependencyCountType = std::uint16_t;

public:
  using VisitedMarkType = std::uint16_t;

  struct PackageNode {
    VersionId version_id_begin;
    VersionCountType version_count;
  };

  struct VersionNode {
    DependencyId dependency_id_begin;
    DependencyCountType dependency_count;
    ArchitectureType architecture;
  };

  struct DependencyEdge {
    DependencyId original_dependency_id;
    PackageId to_package_id;
    ArchitectureType architecture_constraint;
    DependencyType dependency_type;
    GroupId group;
  };

  GpuGraph();
  ~GpuGraph() { free_device(); }

  void build(const DependencyGraph &graph);
  void free_device();

private:
  std::vector<VersionId> to_gpu_version_id_;
  mutable VisitedMarkType mark_;
  PackageNode *d_package_nodes_;
  VersionNode *d_version_nodes_;
  DependencyEdge *d_dependency_edges_;
  mutable VersionId *d_frontier_;
  mutable VersionId *d_next_;
  std::size_t *d_next_size_;
  DependencyId *d_dependency_ids_;
  std::size_t *d_dependency_count_;
  VisitedMarkType *d_visited_;
};
