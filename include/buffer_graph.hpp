#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include "config.hpp"
#include "data_model.hpp"
#include "util.hpp"

namespace xpg {

class BufferGraph {
public:
  BufferGraph(StorageGraph &storage_graph) noexcept : storage_graph_(storage_graph) {}
  BufferGraph(const BufferGraph &) = delete;
  BufferGraph &operator=(const BufferGraph &) = delete;
  BufferGraph(BufferGraph &&) noexcept = default;
  BufferGraph &operator=(BufferGraph &&) = delete;
  ~BufferGraph() noexcept = default;

  std::size_t package_count() const noexcept { return package_nodes_.size(); }
  std::size_t version_count() const noexcept { return version_nodes_.size(); }
  std::size_t dependency_count() const noexcept { return dependency_edges_.size(); }
  bool empty() const noexcept { return package_nodes_.empty(); }

  bool create_package(const PackageInfo &info, bool update_if_exists = false);

  std::size_t estimated_memory_usage() const noexcept;
  void clear() noexcept;
  void flush(bool update_if_exists = false);

  std::variant<DependencyTree, DependencyFlat> query_dependencies(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree) const;
  DependencyTree query_dependency_tree(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth) const;
  DependencyFlat query_dependency_flat(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth) const;

private:
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
    DependencyType type;
    GroupId group;
  };

  StorageGraph &storage_graph_;
  std::vector<PackageNode> package_nodes_;
  std::vector<VersionNode> version_nodes_;
  std::vector<DependencyEdge> dependency_edges_;
  string_map<PackageId> name_to_package_id_;

  std::pair<PackageId, bool> create_package_node(std::string_view name);
  std::pair<VersionId, bool> create_version_node(PackageId pid, std::string_view version, ArchitectureId arch,
                                                 bool update_if_exists = false);
  std::pair<DependencyId, bool> create_dependency_edge(VersionId from_vid, PackageId to_pid, std::string_view vcons,
                                                       ArchitectureId acons, DependencyType type, GroupId group);

  std::vector<VersionId> init_frontier(std::string_view name, std::string_view version,
                                       std::string_view architecture) const;
};

} // namespace xpg
