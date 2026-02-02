#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "config.hpp"
#include "data_model.hpp"
#include "util.hpp"

class BufferGraph {
public:
  BufferGraph(StorageGraph &storage_graph) noexcept : storage_graph_(storage_graph) {}

  BufferGraph(const BufferGraph &) = delete;
  BufferGraph &operator=(const BufferGraph &) = delete;

  BufferGraph(BufferGraph &&) noexcept = default;
  BufferGraph &operator=(BufferGraph &&) noexcept = delete;

  ~BufferGraph() noexcept = default;

  std::size_t estimated_memory_usage() const noexcept;

  std::size_t package_count() const noexcept { return package_nodes_.size(); }
  std::size_t version_count() const noexcept { return version_nodes_.size(); }
  std::size_t dependency_count() const noexcept { return dependency_edges_.size(); }

  bool empty() const noexcept { return package_nodes_.empty(); }

  void clear();

  void create_package(const PackageInfo &info);

  std::vector<VersionId> init_frontier(std::string_view name, std::string_view version, std::string_view arch) const;
  DependencyResult query_dependencies(std::vector<VersionId> &frontier, std::size_t depth) const;

private:
  friend class StorageGraph;

  struct PackageNode;
  struct VersionNode;
  struct DependencyEdge;

  StorageGraph &storage_graph_;
  std::vector<PackageNode> package_nodes_;
  std::vector<VersionNode> version_nodes_;
  std::vector<DependencyEdge> dependency_edges_;
  string_map<PackageId> name_to_package_id_;

  struct PackageNode {
    std::string name;
    std::vector<VersionId> versions;
  };

  struct VersionNode {
    std::string version;
    ArchitectureId architecture;
    std::vector<DependencyId> dependencies;
  };

  struct DependencyEdge {
    VersionId from_version;
    PackageId to_package;
    std::string version_constraint;
    ArchitectureId architecture_constraint;
    DependencyTypeId dependency_type;
    GroupId group;
  };

  std::pair<PackageId, bool> create_package(std::string_view name);
  std::pair<VersionId, bool> create_version(PackageId pid, std::string_view version, ArchitectureId arch);
  std::pair<DependencyId, bool> create_dependency(VersionId from, PackageId to, std::string_view vcons,
                                                  ArchitectureId acons, DependencyTypeId dtype, GroupId group);

  DependencyInfo to_info(DependencyId did) const noexcept;

  DependencyLevel expand_frontier(std::vector<VersionId> &frontier, std::unordered_set<VersionId> &visited,
                                  bool has_next) const;
};
