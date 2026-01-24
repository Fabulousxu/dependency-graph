#pragma once
#include <cstdint>
#include <vector>
#include "config.hpp"
#include "result_model.hpp"

class GpuGraph {
public:
  using VersionCountType = std::uint16_t;
  using DependencyCountType = std::uint16_t;
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

  GpuGraph() noexcept;
  ~GpuGraph() { free(); }

  void build(const DiskGraph &dgraph, std::size_t max_device_vector_bytes = kDefaultMaxDeviceVectorBytes);
  void free();

private:
  friend class DependencyGraph;

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
