#include "buffer_graph.hpp"
#include <ranges>
#include "storage_graph.hpp"

std::size_t BufferGraph::estimated_memory_usage() const noexcept {
  auto total = sizeof(BufferGraph);
  total += package_nodes_.size() * sizeof(PackageNode);
  for (const auto &pnode : package_nodes_) {
    total += pnode.name.capacity() * sizeof(char);
    total += pnode.versions.capacity() * sizeof(VersionId);
  }
  total += version_nodes_.size() * sizeof(VersionNode);
  for (const auto &vnode : version_nodes_) {
    total += vnode.version.capacity() * sizeof(char);
    total += vnode.dependencies.capacity() * sizeof(DependencyId);
  }
  total += dependency_edges_.size() * sizeof(DependencyEdge);
  total += name_to_package_id_.bucket_count() * sizeof(void *);
  total += name_to_package_id_.size() * (sizeof(std::pair<std::string, PackageId>) + sizeof(void *));
  for (const auto &name : name_to_package_id_ | std::views::keys) total += name.capacity() * sizeof(char);
  return total;
}

std::pair<PackageId, bool> BufferGraph::create_package(std::string_view name) {
  auto pid = static_cast<PackageId>(package_count());
  auto [it, succ] = name_to_package_id_.emplace(name, pid);
  if (!succ) return std::make_pair(it->second, false);
  package_nodes_.push_back({.name = std::string(name)});
  return std::make_pair(pid, true);
}

std::pair<VersionId, bool> BufferGraph::create_version(PackageId pid, std::string_view version, ArchitectureId arch) {
  auto &pnode = package_nodes_[pid];
  for (auto vid : pnode.versions) {
    const auto &vnode = version_nodes_[vid];
    if (vnode.version == version && vnode.architecture == arch) return std::make_pair(vid, false);
  }
  auto vid = static_cast<VersionId>(version_count());
  version_nodes_.push_back({.version = std::string(version), .architecture = arch});
  pnode.versions.push_back(vid);
  return std::make_pair(vid, true);
}

std::pair<DependencyId, bool> BufferGraph::create_dependency(
  VersionId from, PackageId to, std::string_view vcons, ArchitectureId acons, DependencyTypeId dtype, GroupId group) {
  auto &fvnode = version_nodes_[from];
  auto did = static_cast<DependencyId>(dependency_count());
  dependency_edges_.push_back({
    .from_version = from,
    .to_package = to,
    .version_constraint = std::string(vcons),
    .architecture_constraint = acons,
    .dependency_type = dtype,
    .group = group
  });
  fvnode.dependencies.push_back(did);
  return std::make_pair(did, true);
}

void BufferGraph::create_package(const PackageInfo &info) {
  auto [pid, succ] = create_package(info.name);
  auto arch = storage_graph_.intern_architecture(info.architecture);
  auto [vid, vsucc] = create_version(pid, info.version, arch);
  if (!vsucc) return;
  for (const auto &dinfo : info.direct_dependencies) {
    auto [tpid, tpsucc] = create_package(dinfo.package_name);
    auto acons = storage_graph_.intern_architecture(dinfo.architecture_constraint);
    auto dtype = storage_graph_.intern_dependency_type(dinfo.dependency_type);
    create_dependency(vid, tpid, dinfo.version_constraint, acons, dtype, 0);
  }
  auto gid = static_cast<GroupId>(1);
  for (const auto &group : info.or_dependencies) {
    for (const auto &ginfo : group) {
      auto [tpid, tpsucc] = create_package(ginfo.package_name);
      auto acons = storage_graph_.intern_architecture(ginfo.architecture_constraint);
      auto dtype = storage_graph_.intern_dependency_type(ginfo.dependency_type);
      create_dependency(vid, tpid, ginfo.version_constraint, acons, dtype, gid);
    }
    ++gid;
  }
}

void BufferGraph::clear() {
  package_nodes_.clear();
  version_nodes_.clear();
  dependency_edges_.clear();
  name_to_package_id_.clear();
}

DependencyInfo BufferGraph::to_info(DependencyId did) const noexcept {
  const auto &dedge = dependency_edges_[did];
  const auto &tpnode = package_nodes_[dedge.to_package];
  return {
    .package_name = tpnode.name,
    .dependency_type = storage_graph_.dependency_types()[dedge.dependency_type],
    .version_constraint = dedge.version_constraint,
    .architecture_constraint = storage_graph_.architectures()[dedge.architecture_constraint]
  };
}

std::vector<VersionId> BufferGraph::init_frontier(std::string_view name, std::string_view version,
                                                  std::string_view arch) const {
  std::vector<VersionId> frontier;
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return frontier;
  const auto &pnode = package_nodes_[it->second];
  for (auto vid : pnode.versions) {
    const auto &vnode = version_nodes_[vid];
    if (!version.empty() && vnode.version != version) continue;
    if (!arch.empty() && storage_graph_.architectures()[vnode.architecture] != arch) continue;
    frontier.emplace_back(vid);
  }
  return frontier;
}

DependencyLevel BufferGraph::expand_frontier(std::vector<VersionId> &frontier, std::unordered_set<VersionId> &visited,
                                             bool has_next) const {
  DependencyLevel result;
  std::vector<VersionId> next;
  std::unordered_set<DependencyInfo> visited_dinfos;
  for (auto vid : frontier) {
    const auto &vnode = version_nodes_[vid];
    std::vector<DependencyGroup> groups;
    std::vector<std::unordered_set<DependencyInfo>> visited_ginfos;

    for (auto did : vnode.dependencies) {
      const auto &dedge = dependency_edges_[did];
      const auto &tpnode = package_nodes_[dedge.to_package];
      auto info = to_info(did);

      if (dedge.group > 0) {
        if (groups.size() < dedge.group) {
          groups.resize(dedge.group);
          visited_ginfos.resize(dedge.group);
        }
        auto [it, succ] = visited_ginfos[dedge.group - 1].emplace(info);
        if (succ) groups[dedge.group - 1].emplace_back(std::move(info));
      } else {
        auto [it, succ] = visited_dinfos.emplace(info);
        if (succ) result.direct_dependencies.emplace_back(std::move(info));
      }

      if (has_next && storage_graph_.dependency_types()[dedge.dependency_type] == "Depends" && dedge.group == 0)
        for (auto nvid : tpnode.versions) {
          if (visited.contains(nvid)) continue;
          const auto &nvnode = version_nodes_[nvid];
          if (storage_graph_.match_architecture(vnode.architecture, nvnode.architecture,
                                                dedge.architecture_constraint)) {
            next.emplace_back(nvid);
            visited.emplace(nvid);
          }
        }
    }
    for (auto &group : groups) if (!group.empty()) result.or_dependencies.emplace_back(std::move(group));
  }
  frontier = std::move(next);
  return result;
}

DependencyResult BufferGraph::query_dependencies(std::vector<VersionId> &frontier, std::size_t depth) const {
  DependencyResult result(depth);
  if (frontier.empty()) return result;
  std::unordered_set visited(frontier.begin(), frontier.end());
  for (auto level = 0; level < depth; ++level) {
    result[level] = expand_frontier(frontier, visited, level + 1 < depth);
    if (frontier.empty()) break;
  }
  return result;
}
