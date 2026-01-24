#include "buffer_graph.hpp"
#include <ranges>

auto BufferGraph::get_package(std::string_view name) const noexcept
  -> std::optional<std::reference_wrapper<const PackageNode>> {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return std::cref(package_nodes_[it->second]);
  return std::nullopt;
}

std::pair<PackageId, bool> BufferGraph::create_package(std::string_view name) {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return {it->second, false};
  PackageId pid = package_count();
  package_nodes_.push_back({
    .name = std::string(name)
  });
  name_to_package_id_.emplace(name, pid);
  return {pid, true};
}

std::pair<VersionId, bool> BufferGraph::create_version(PackageId pid, std::string_view version, ArchitectureType arch) {
  auto &pnode = package_nodes_[pid];
  for (auto vid : pnode.version_ids) {
    const auto &vnode = version_nodes_[vid];
    if (vnode.version == version && vnode.architecture == arch) return {vid, false};
  }
  VersionId vid = version_count();
  version_nodes_.push_back({
    .version = std::string(version),
    .architecture = arch
  });
  pnode.version_ids.push_back(vid);
  return {vid, true};
}

std::pair<DependencyId, bool> BufferGraph::create_dependency(VersionId from_vid, PackageId to_pid,
                                                             std::string_view vcons, ArchitectureType acons,
                                                             DependencyType dtype, GroupId gid) {
  auto &fvnode = version_nodes_[from_vid];
  DependencyId did = dependency_count();
  dependency_edges_.push_back({
    .from_version_id = from_vid,
    .to_package_id = to_pid,
    .version_constraint = std::string(vcons),
    .architecture_constraint = acons,
    .dependency_type = dtype,
    .group = gid
  });
  fvnode.dependency_ids.push_back(did);
  return {did, true};
}

void BufferGraph::clear() {
  package_nodes_.clear();
  version_nodes_.clear();
  dependency_edges_.clear();
  name_to_package_id_.clear();
}

std::size_t BufferGraph::estimated_memory_usage() const noexcept {
  std::size_t total = sizeof(BufferGraph);
  total += package_nodes_.size() * sizeof(PackageNode);
  for (const auto &pnode : package_nodes_) {
    total += pnode.name.capacity() * sizeof(char);
    total += pnode.version_ids.capacity() * sizeof(VersionId);
  }

  total += version_nodes_.size() * sizeof(VersionNode);
  for (const auto &vnode : version_nodes_) {
    total += vnode.version.capacity() * sizeof(char);
    total += vnode.dependency_ids.capacity() * sizeof(DependencyId);
  }

  total += dependency_edges_.size() * sizeof(DependencyEdge);
  total += name_to_package_id_.bucket_count() * sizeof(void *);
  total += name_to_package_id_.size() * (sizeof(std::pair<std::string, PackageId>) + sizeof(void *));
  for (const auto &name : name_to_package_id_ | std::views::keys) total += name.capacity() * sizeof(char);
  return total;
}
