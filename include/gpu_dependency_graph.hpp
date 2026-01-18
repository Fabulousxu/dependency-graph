#pragma once
#include "types.hpp"

class GpuDependencyGraph {
public:
  struct PackageNode {
    VersionId start_version_id;
    DegreeType version_count;
  };

  struct VersionNode {
    DependencyId start_dependency_id;
    DegreeType dependency_count;
    ArchitectureType architecture;
  };

  struct DependencyEdge {
    DependencyId original_dependency_id;
    VersionId from_version_id;
    PackageId to_package_id;
    ArchitectureType architecture_constraint;
    DependencyType dependency_type;
    GroupId group;
  };

  GpuDependencyGraph(DependencyGraph &graph) : graph_{graph} {};
  ~GpuDependencyGraph() { free_device(); }

  void build(bool verbose = false);

  DependencyResult query_dependencies(const std::vector<VersionId> &vids, std::size_t depth);

private:
  DependencyGraph &graph_;
  std::vector<PackageNode> package_nodes_;
  std::vector<VersionNode> version_nodes_;
  std::vector<DependencyEdge> dependency_edges_;
  std::vector<VersionId> to_cuda_version_id_;
  std::vector<VersionId> frontier_;
  std::vector<DependencyId> dependency_ids_;
  VisitedMark mark_ = 1;
  PackageNode *d_package_nodes_ = nullptr;
  VersionNode *d_version_nodes_ = nullptr;
  DependencyEdge *d_dependency_edges_ = nullptr;
  VersionId *d_frontier_ = nullptr;
  VersionId *d_next_ = nullptr;
  DependencyId *d_dependency_ids_ = nullptr;
  std::size_t *d_next_size_ = nullptr;
  std::size_t *d_dependency_count_ = nullptr;
  VisitedMark *d_visited_ = nullptr;

  void to_device();
  void free_device();
};

constexpr auto MAX_DEVICE_VECTOR_SIZE = 16 * 1024 * 1024;
