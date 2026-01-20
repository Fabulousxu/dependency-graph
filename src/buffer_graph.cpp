#include "buffer_graph.hpp"

#include <ranges>

std::pair<PackageId, bool> BufferGraph::create_package(std::string_view name) {
  PackageId pid = package_count();
  auto [it, succ] = name_to_package_id_.emplace(name, pid);
  if (!succ) return {it->second, false};
  auto &pnode = package_nodes_.emplace_back();
  pnode.name = name;
  return {pid, true};
}

std::pair<VersionId, bool> BufferGraph::create_version(PackageId pid, std::string_view version,
  ArchitectureType arch) {
  auto &pnode = package_nodes_[pid];
  for (auto vid : pnode.version_ids) {
    const auto &vnode = version_nodes_[vid];
    if (vnode.version == version && vnode.architecture == arch) return {vid, false};
  }
  VersionId vid = version_count();
  pnode.version_ids.push_back(vid);
  auto &vnode = version_nodes_.emplace_back();
  vnode.version = version;
  vnode.architecture = arch;
  return {vid, true};
}

std::pair<DependencyId, bool> BufferGraph::create_dependency(VersionId from_vid, PackageId to_pid,
  std::string_view version_constr, ArchitectureType arch_constr, DependencyType dep_type, GroupId group) {
  DependencyId did = dependency_count();
  auto &fvnode = version_nodes_[from_vid];
  fvnode.dependency_ids.push_back(did);
  auto &dedge = dependency_edges_.emplace_back();
  dedge.from_version_id = from_vid;
  dedge.to_package_id = to_pid;
  dedge.version_constraint = version_constr;
  dedge.architecture_constraint = arch_constr;
  dedge.dependency_type = dep_type;
  dedge.group = group;
  return {did, true};
}

void BufferGraph::clear() {
  package_nodes_.clear();
  version_nodes_.clear();
  dependency_edges_.clear();
  name_to_package_id_.clear();
}

std::size_t BufferGraph::memory_usage() const noexcept {
  auto total = sizeof(BufferGraph);
  total += package_nodes_.capacity() * sizeof(PackageNode);
  for (const auto &pnode : package_nodes_) {
    total += pnode.name.capacity() * sizeof(char);
    total += pnode.version_ids.capacity() * sizeof(VersionId);
  }
  total += version_nodes_.capacity() * sizeof(VersionNode);
  for (const auto &vnode : version_nodes_) {
    total += vnode.version.capacity() * sizeof(char);
    total += vnode.dependency_ids.capacity() * sizeof(DependencyId);
  }
  total += dependency_edges_.capacity() * sizeof(DependencyEdge);
  total += name_to_package_id_.bucket_count() * sizeof(void *);
  total += name_to_package_id_.size() * (sizeof(std::pair<std::string, PackageId>) + sizeof(void *));
  for (const auto &name : name_to_package_id_ | std::views::keys) total += name.capacity() * sizeof(char);
  return total;
}
