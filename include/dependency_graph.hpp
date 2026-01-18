#pragma once
#include "types.hpp"

class DependencyGraph {
public:
  struct PackageNode {
    std::string name;
    std::vector<VersionId> version_ids;
  };

  struct VersionNode {
    std::string version;
    ArchitectureType architecture;
    std::vector<DependencyId> dependency_ids;
  };

  struct DependencyEdge {
    VersionId from_version_id;
    PackageId to_package_id;
    std::string version_constraint;
    ArchitectureType architecture_constraint;
    DependencyType dependency_type;
    GroupId group;
  };

  SymbolTable<ArchitectureType> architectures;
  SymbolTable<DependencyType> dependency_types;

  DependencyGraph();
  ~DependencyGraph();

  std::size_t package_count() const noexcept { return package_nodes_.size(); }
  std::size_t version_count() const noexcept { return version_nodes_.size(); }
  std::size_t dependency_count() const noexcept { return dependency_edges_.size(); }

  const PackageNode &get_package(PackageId pid) const noexcept { return package_nodes_[pid]; }
  const VersionNode &get_version(VersionId vid) const noexcept { return version_nodes_[vid]; }
  const DependencyEdge &get_dependency(DependencyId did) const noexcept { return dependency_edges_[did]; }

  const PackageNode &create_package(std::string_view name);
  const VersionNode &create_version(const PackageNode &pnode, std::string_view version, ArchitectureType arch);
  const DependencyEdge &create_dependency(const VersionNode &from, const PackageNode &to,
    std::string_view version_constr, ArchitectureType arch_constr, DependencyType dep_type, GroupId group);

  DependencyItem to_item(const DependencyEdge &dedge) const noexcept;

  void build_gpu_graph(bool verbose = false) const;

  DependencyResult query_dependencies(std::string_view name, std::string_view version, std::string_view arch,
    std::size_t depth, bool use_gpu) const;

private:
  GpuDependencyGraph *gpu_graph_;
  std::vector<PackageNode> package_nodes_;
  std::vector<VersionNode> version_nodes_;
  std::vector<DependencyEdge> dependency_edges_;
  StringKeyHashMap<PackageId> name_to_package_id_;

  PackageId id(const PackageNode &pnode) const noexcept { return &pnode - package_nodes_.data(); }
  VersionId id(const VersionNode &vnode) const noexcept { return &vnode - version_nodes_.data(); }
  DependencyId id(const DependencyEdge &dedge) const noexcept { return &dedge - dependency_edges_.data(); }
};
