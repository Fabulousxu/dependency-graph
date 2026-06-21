#include "buffer_graph.hpp"
#include <ranges>
#include "storage_graph.hpp"

namespace xpg {

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

PackageView BufferGraph::get_package(PackageId pid) const noexcept {
  return {
    package_nodes_[pid].name, [this, pid] {
      std::vector<VersionView> vviews;
      for (auto vid : package_nodes_[pid].versions) vviews.emplace_back(get_version(vid));
      return vviews;
    }
  };
}

VersionView BufferGraph::get_version(VersionId vid) const noexcept {
  return {
    version_nodes_[vid].version, storage_graph_.architectures_[version_nodes_[vid].architecture],
    [this, vid] {
      std::vector<DependencyView> dviews;
      for (auto did : version_nodes_[vid].dependencies)
        if (dependency_edges_[did].group == 0) dviews.emplace_back(get_dependency(did));
      return dviews;
    },
    [this, vid] {
      std::vector<std::vector<DependencyView>> gviews;
      for (auto did : version_nodes_[vid].dependencies) {
        const auto &dedge = dependency_edges_[did];
        if (dedge.group == 0) continue;
        if (gviews.size() < dedge.group) gviews.resize(dedge.group);
        gviews[dedge.group - 1].emplace_back(get_dependency(did));
      }
      std::erase_if(gviews, [](const auto &gview) { return gview.empty(); });
      return gviews;
    }
  };
}

DependencyView BufferGraph::get_dependency(DependencyId did) const noexcept {
  return {
    [this, did] { return get_version(dependency_edges_[did].from_version); },
    [this, did] { return get_package(dependency_edges_[did].to_package); },
    storage_graph_.dependency_types_[dependency_edges_[did].type], dependency_edges_[did].version_constraint,
    storage_graph_.architectures_[dependency_edges_[did].architecture_constraint],
  };
}

std::optional<PackageView> BufferGraph::get_package(std::string_view name) const noexcept {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return get_package(it->second);
  return std::nullopt;
}

bool BufferGraph::create_package(const PackageInfo &info, bool update_if_exists) {
  auto [pid, succ] = create_package_node(info.name);
  auto arch = storage_graph_.intern_architecture(info.architecture);
  auto [vid, vsucc] = create_version_node(pid, info.version, arch, update_if_exists);
  if (!vsucc) return false;
  auto create_dependency_edge_from_info = [this, vid](const DependencyInfo &info, GroupId group) {
    auto [pid, pcuss] = create_package_node(info.name);
    auto arch = storage_graph_.intern_architecture(info.architecture);
    auto type = storage_graph_.intern_dependency_type(info.type);
    create_dependency_edge(vid, pid, info.version_constraint, arch, type, group);
  };
  for (const auto &dinfo : info.single_dependencies) create_dependency_edge_from_info(dinfo, 0);
  auto gid = static_cast<GroupId>(1);
  for (const auto &group : info.alternative_dependencies) {
    for (const auto &ginfo : group) create_dependency_edge_from_info(ginfo, gid);
    ++gid;
  }
  return true;
}

std::pair<PackageId, bool> BufferGraph::create_package_node(std::string_view name) {
  auto pid = static_cast<PackageId>(package_nodes_.size());
  auto [it, succ] = name_to_package_id_.emplace(name, pid);
  if (!succ) return {it->second, false};
  package_nodes_.push_back({std::string(name)});
  return {pid, true};
}

std::pair<VersionId, bool> BufferGraph::create_version_node(PackageId pid, std::string_view version,
                                                            ArchitectureId arch, bool update_if_exists) {
  auto &versions = package_nodes_[pid].versions;
  for (auto it = versions.begin(); it != versions.end(); ++it) {
    const auto &vnode = version_nodes_[*it];
    if (vnode.version != version || vnode.architecture != arch) continue;
    if (!update_if_exists) return {*it, false};
    versions.erase(it);
    break;
  }
  auto vid = static_cast<VersionId>(version_nodes_.size());
  version_nodes_.push_back({std::string(version), arch});
  versions.emplace_back(vid);
  return {vid, true};
}

std::pair<DependencyId, bool> BufferGraph::create_dependency_edge(VersionId from_vid, PackageId to_pid,
                                                                  std::string_view vcons, ArchitectureId acons,
                                                                  DependencyType type, GroupId group) {
  auto did = static_cast<DependencyId>(dependency_edges_.size());
  dependency_edges_.push_back({from_vid, to_pid, std::string(vcons), acons, type, group});
  version_nodes_[from_vid].dependencies.push_back(did);
  return {did, true};
}

void BufferGraph::clear() noexcept {
  package_nodes_.clear();
  version_nodes_.clear();
  dependency_edges_.clear();
  name_to_package_id_.clear();
}

void BufferGraph::flush(bool update_if_existes) {
  for (const auto &pnode : package_nodes_) {
    auto vbegin = static_cast<VersionId>(storage_graph_.version_nodes_.size());
    auto vcount = static_cast<VersionCount>(0);
    auto [spid, spsucc] = storage_graph_.create_package_node(pnode.name);
    for (auto vid : pnode.versions) {
      const auto &[version, architecture, dependencies] = version_nodes_[vid];
      auto dbegin = static_cast<DependencyId>(storage_graph_.dependency_edges_.size());
      auto dcount = static_cast<DependencyCount>(0);
      auto [svid, svsucc] = storage_graph_.create_version_node(spid, version, architecture, update_if_existes);
      if (!svsucc) continue;
      ++vcount;
      for (auto did : dependencies) {
        const auto &bdedge = dependency_edges_[did];
        auto [spid, spsucc] = storage_graph_.create_package_node(package_nodes_[bdedge.to_package].name);
        auto [sdid, sdsucc] = storage_graph_.create_dependency_edge(
          svid, spid, bdedge.version_constraint, bdedge.architecture_constraint, bdedge.type, bdedge.group);
        if (sdsucc) ++dcount;
      }
      storage_graph_.attach_dependencies(svid, dbegin, dcount);
    }
    storage_graph_.attach_versions(spid, vbegin, vcount);
  }
  clear();
}

std::variant<DependencyTree, DependencyFlat> BufferGraph::query_dependencies(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree) const {
  if (tree) return query_dependency_tree(name, version, architecture, depth);
  return query_dependency_flat(name, version, architecture, depth);
}

std::vector<VersionId> BufferGraph::init_frontier(std::string_view name, std::string_view version,
                                                  std::string_view architecture) const {
  std::vector<VersionId> frontier;
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return frontier;
  auto arch = architecture.empty() ? kNullArchitecture
                : storage_graph_.architectures_.id(architecture).value_or(kNullArchitecture);
  for (auto vid : package_nodes_[it->second].versions) {
    const auto &vnode = version_nodes_[vid];
    if (!version.empty() && vnode.version != version) continue;
    if (!architecture.empty() && vnode.architecture != arch && vnode.architecture != kAllArchitecture) continue;
    frontier.emplace_back(vid);
  }
  return frontier;
}

DependencyTree BufferGraph::query_dependency_tree(std::string_view name, std::string_view version,
                                                  std::string_view architecture, std::size_t depth) const {
  DependencyTree result;
  auto frontier = init_frontier(name, version, architecture);
  std::unordered_set visited(frontier.begin(), frontier.end());
  auto expand_tree = [&, this, depth]
  (const auto &self, VersionId vid, DependencyTree &tree, std::size_t level) -> void {
    for (auto did : version_nodes_[vid].dependencies) {
      const auto &dedge = dependency_edges_[did];
      DependencyTree subtree{
        package_nodes_[dedge.to_package].name, storage_graph_.dependency_types_[dedge.type], dedge.version_constraint,
        storage_graph_.architectures_[dedge.architecture_constraint]
      };
      if (level + 1 < depth)
        for (auto vid : package_nodes_[dedge.to_package].versions) {
          if (visited.contains(vid)) continue;
          if (!StorageGraph::match_architecture(version_nodes_[vid].architecture, dedge.architecture_constraint))
            continue;
          visited.insert(vid);
          self(self, vid, subtree, level + 1);
        }
      if (dedge.group != 0) {
        if (tree.alternative_dependencies.size() < dedge.group) tree.alternative_dependencies.resize(dedge.group);
        tree.alternative_dependencies[dedge.group - 1].emplace_back(std::move(subtree));
      } else tree.single_dependencies.emplace_back(std::move(subtree));
    }
  };
  for (auto vid : frontier) expand_tree(expand_tree, vid, result, 0);
  return result;
}

DependencyFlat BufferGraph::query_dependency_flat(std::string_view name, std::string_view version,
                                                  std::string_view architecture, std::size_t depth) const {
  DependencyFlat result(depth);
  auto frontier = init_frontier(name, version, architecture);
  std::unordered_set visited(frontier.begin(), frontier.end());
  for (auto level = 0; level < depth; ++level) {
    std::vector<VersionId> next;
    std::unordered_set<DependencyInfo> dvisited;
    for (auto vid : frontier) {
      std::vector<std::vector<DependencyInfo>> groups;
      std::vector<std::unordered_set<DependencyInfo>> gvisited;
      for (auto did : version_nodes_[vid].dependencies) {
        const auto &dedge = dependency_edges_[did];
        DependencyInfo info{
          package_nodes_[dedge.to_package].name, storage_graph_.dependency_types_[dedge.type], dedge.version_constraint,
          storage_graph_.architectures_[dedge.architecture_constraint]
        };
        if (dedge.group > 0) {
          if (groups.size() < dedge.group) {
            groups.resize(dedge.group);
            gvisited.resize(dedge.group);
          }
          auto [it, succ] = gvisited[dedge.group - 1].emplace(info);
          if (succ) groups[dedge.group - 1].emplace_back(std::move(info));
        } else {
          auto [it, succ] = dvisited.emplace(info);
          if (succ) result[level].single_dependencies.emplace_back(std::move(info));
        }
        if (level + 1 < depth)
          for (auto vid : package_nodes_[dedge.to_package].versions) {
            if (visited.contains(vid)) continue;
            if (!StorageGraph::match_architecture(version_nodes_[vid].architecture, dedge.architecture_constraint))
              continue;
            next.emplace_back(vid);
            visited.emplace(vid);
          }
      }
      for (auto &group : groups)
        if (!group.empty()) result[level].alternative_dependencies.emplace_back(std::move(group));
    }
    frontier = std::move(next);
    if (frontier.empty()) break;
  }
  return result;
}

} // namespace xpg
