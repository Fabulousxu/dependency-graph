#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "config.hpp"
#include "string_map.hpp"

class BufferGraph {
  friend class DiskGraph;
  friend class DependencyGraph;

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

public:
  BufferGraph() = default;
  ~BufferGraph() = default;

  std::size_t package_count() const noexcept { return package_nodes_.size(); }
  std::size_t version_count() const noexcept { return version_nodes_.size(); }
  std::size_t dependency_count() const noexcept { return dependency_edges_.size(); }

  std::pair<PackageId, bool> create_package(std::string_view name);
  std::pair<VersionId, bool> create_version(PackageId pid, std::string_view version, ArchitectureType arch);
  std::pair<DependencyId, bool> create_dependency(VersionId from_vid, PackageId to_pid, std::string_view version_constr,
    ArchitectureType arch_constr, DependencyType dep_type, GroupId group);

  void clear();

  std::size_t estimated_memory_usage() const noexcept;

private:
  std::vector<PackageNode> package_nodes_;
  std::vector<VersionNode> version_nodes_;
  std::vector<DependencyEdge> dependency_edges_;
  StringMap<PackageId> name_to_package_id_;
};
