#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include "config.hpp"
#include "types.hpp"
#include "utils.hpp"

namespace xpg {

class WriteBuffer {
  struct PackageNode {
    std::string name;
    std::vector<VersionId> versions;
  };

  struct VersionNode {
    std::string version;
    ArchitectureType architecture;
    std::vector<DependencyId> dependencies;
  };

  struct DependencyEdge {
    VersionId from_version;
    PackageId to_package;
    std::string version_constraint;
    ArchitectureType architecture_constraint;
    DependencyType type;
    DependencyGroupId group;
  };

public:
  WriteBuffer(XPGraph &graph) noexcept : graph_(graph) {}
  WriteBuffer(const WriteBuffer &) = delete;
  WriteBuffer &operator=(const WriteBuffer &) = delete;
  WriteBuffer(WriteBuffer &&) noexcept = default;
  WriteBuffer &operator=(WriteBuffer &&other) noexcept;
  ~WriteBuffer() noexcept = default;

  std::size_t estimated_memory_usage() const noexcept;
  void clear() noexcept;
  void flush(bool update_if_exists = false);

  std::size_t package_count() const noexcept { return package_nodes_.size(); }
  std::size_t version_count() const noexcept { return version_nodes_.size(); }
  std::size_t dependency_count() const noexcept { return dependency_edges_.size(); }
  bool empty() const noexcept { return package_nodes_.empty(); }

  PackageView get_package(PackageId pid) const noexcept;
  std::optional<PackageView> get_package(std::string_view name) const noexcept;
  bool create_package(const PackageInfo &info, bool update_if_exists = false);

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
  XPGraph &graph_;
  std::vector<PackageNode> package_nodes_;
  std::vector<VersionNode> version_nodes_;
  std::vector<DependencyEdge> dependency_edges_;
  string_map<PackageId> name_to_package_id_;

  VersionView get_version(VersionId vid) const noexcept;
  DependencyView get_dependency(DependencyId did) const noexcept;

  std::pair<PackageId, bool> create_package_node(std::string_view name);
  std::pair<VersionId, bool> create_version_node(PackageId pid, std::string_view version, ArchitectureType arch,
                                                 bool update_if_exists = false);
  std::pair<DependencyId, bool> create_dependency_edge(VersionId from_vid, PackageId to_pid, std::string_view vcons,
                                                       ArchitectureType acons, DependencyType type,
                                                       DependencyGroupId group);

  std::vector<VersionId> init_frontier(std::string_view name, std::string_view version, ArchitectureType arch) const;
};

} // namespace xpg
