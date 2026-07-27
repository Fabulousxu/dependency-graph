#include "write_buffer.hpp"
#include <ranges>
#include "xpgraph.hpp"

namespace xpg {

WriteBuffer &WriteBuffer::operator=(WriteBuffer &&other) noexcept {
  if (this == &other) return *this;
  package_nodes_ = std::move(other.package_nodes_);
  version_nodes_ = std::move(other.version_nodes_);
  dependency_edges_ = std::move(other.dependency_edges_);
  name_to_package_id_ = std::move(other.name_to_package_id_);
  return *this;
}

std::size_t WriteBuffer::estimated_memory_usage() const noexcept {
  auto total = sizeof(WriteBuffer);
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

void WriteBuffer::clear() noexcept {
  package_nodes_.clear();
  version_nodes_.clear();
  dependency_edges_.clear();
  name_to_package_id_.clear();
}

void WriteBuffer::flush(bool update_if_existes) {
  for (const auto &pnode : package_nodes_) {
    auto vbegin = static_cast<VersionId>(graph_.version_nodes_.size());
    auto vcount = static_cast<VersionCount>(0);
    auto [pid, psucc] = graph_.create_package_node(pnode.name);
    for (auto bvid : pnode.versions) {
      const auto &[version, architecture, dependencies] = version_nodes_[bvid];
      auto dbegin = static_cast<DependencyId>(graph_.dependency_edges_.size());
      auto dcount = static_cast<DependencyCount>(0);
      auto [vid, vsucc] = graph_.create_version_node(pid, version, architecture, update_if_existes);
      if (!vsucc) continue;
      ++vcount;
      for (auto bdid : dependencies) {
        const auto &bdedge = dependency_edges_[bdid];
        auto [pid, psucc] = graph_.create_package_node(package_nodes_[bdedge.to_package].name);
        auto [did, dsucc] = graph_.create_dependency_edge(vid, pid, bdedge.version_constraint,
                                                          bdedge.architecture_constraint, bdedge.type, bdedge.group);
        if (dsucc) ++dcount;
      }
      graph_.attach_dependencies(vid, dbegin, dcount);
    }
    graph_.attach_versions(pid, vbegin, vcount);
  }
  clear();
}

PackageView WriteBuffer::get_package(PackageId pid) const noexcept {
  PackageView pview;
  const auto &pnode = package_nodes_[pid];
  pview.name = pnode.name;
  pview.versions = [this, pid] {
    std::vector<VersionView> vviews;
    for (auto vid : package_nodes_[pid].versions) vviews.emplace_back(get_version(vid));
    return vviews;
  };
  return pview;
}

std::optional<PackageView> WriteBuffer::get_package(std::string_view name) const noexcept {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return get_package(it->second);
  return std::nullopt;
}

VersionView WriteBuffer::get_version(VersionId vid) const noexcept {
  VersionView vview;
  const auto &vnode = version_nodes_[vid];
  vview.version = vnode.version;
  vview.architecture = graph_.architecture_types_[vnode.architecture];
  vview.single_dependencies = [this, vid] {
    std::vector<DependencyView> dviews;
    for (auto did : version_nodes_[vid].dependencies)
      if (dependency_edges_[did].group == 0) dviews.emplace_back(get_dependency(did));
    return dviews;
  };
  vview.alternative_dependencies = [this, vid] {
    std::vector<std::vector<DependencyView>> gviews;
    for (auto did : version_nodes_[vid].dependencies) {
      const auto &dedge = dependency_edges_[did];
      if (dedge.group == 0) continue;
      if (gviews.size() < dedge.group) gviews.resize(dedge.group);
      gviews[dedge.group - 1].emplace_back(get_dependency(did));
    }
    std::erase_if(gviews, [](const auto &gview) { return gview.empty(); });
    return gviews;
  };
  return vview;
}

DependencyView WriteBuffer::get_dependency(DependencyId did) const noexcept {
  DependencyView dview;
  const auto &dedge = dependency_edges_[did];
  dview.from_version = [this, did] { return get_version(dependency_edges_[did].from_version); };
  dview.to_package = [this, did] { return get_package(dependency_edges_[did].to_package); };
  dview.type = graph_.dependency_types_[dedge.type];
  dview.version_constraint = dedge.version_constraint;
  dview.architecture_constraint = graph_.architecture_types_[dedge.architecture_constraint];
  return dview;
}

bool WriteBuffer::create_package(const PackageInfo &info, bool update_if_exists) {
  auto [pid, succ] = create_package_node(info.name);
  auto arch = graph_.intern_architecture(info.architecture);
  auto [vid, vsucc] = create_version_node(pid, info.version, arch, update_if_exists);
  if (!vsucc) return false;
  auto create_dependency_edge_from_info = [this, vid](const DependencyInfo &info, DependencyGroupId group) {
    auto [pid, pcuss] = create_package_node(info.name);
    auto arch = graph_.intern_architecture(info.architecture_constraint);
    auto type = graph_.intern_dependency(info.type);
    create_dependency_edge(vid, pid, info.version_constraint, arch, type, group);
  };
  for (const auto &dinfo : info.single_dependencies) create_dependency_edge_from_info(dinfo, 0);
  auto gid = static_cast<DependencyGroupId>(1);
  for (const auto &group : info.alternative_dependencies) {
    for (const auto &ginfo : group) create_dependency_edge_from_info(ginfo, gid);
    ++gid;
  }
  return true;
}

std::pair<PackageId, bool> WriteBuffer::create_package_node(std::string_view name) {
  auto pid = static_cast<PackageId>(package_nodes_.size());
  auto [it, succ] = name_to_package_id_.emplace(name, pid);
  if (!succ) return {it->second, false};
  auto &pnode = package_nodes_.emplace_back();
  pnode.name = name;
  return {pid, true};
}

std::pair<VersionId, bool> WriteBuffer::create_version_node(PackageId pid, std::string_view version,
                                                            ArchitectureType arch, bool update_if_exists) {
  auto &versions = package_nodes_[pid].versions;
  for (auto it = versions.begin(); it != versions.end(); ++it) {
    const auto &vnode = version_nodes_[*it];
    if (vnode.version != version || vnode.architecture != arch) continue;
    if (!update_if_exists) return {*it, false};
    versions.erase(it);
    break;
  }
  auto vid = static_cast<VersionId>(version_nodes_.size());
  auto &vnode = version_nodes_.emplace_back();
  vnode.version = version;
  vnode.architecture = arch;
  versions.emplace_back(vid);
  return {vid, true};
}

std::pair<DependencyId, bool> WriteBuffer::create_dependency_edge(
  VersionId from_vid, PackageId to_pid, std::string_view vcons, ArchitectureType acons, DependencyType type,
  DependencyGroupId group) {
  auto did = static_cast<DependencyId>(dependency_edges_.size());
  auto &dedge = dependency_edges_.emplace_back();
  dedge.from_version = from_vid;
  dedge.to_package = to_pid;
  dedge.type = type;
  dedge.version_constraint = vcons;
  dedge.architecture_constraint = acons;
  dedge.group = group;
  version_nodes_[from_vid].dependencies.push_back(did);
  return {did, true};
}

std::variant<DependencyTree, DependencyFlat> WriteBuffer::query_dependencies(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  if (tree)
    return query_dependency_tree(name, version, architecture, depth, satisfy_architecture, satisfy_version,
                                 expand_alternative);
  return query_dependency_flat(name, version, architecture, depth, satisfy_architecture, satisfy_version,
                               expand_alternative);
}

std::vector<VersionId> WriteBuffer::init_frontier(std::string_view name, std::string_view version,
                                                  ArchitectureType arch) const {
  std::vector<VersionId> frontier;
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return frontier;
  for (auto vid : package_nodes_[it->second].versions) {
    const auto &vnode = version_nodes_[vid];
    if (!version.empty() && vnode.version != version) continue;
    if (arch != ArchitectureType::kNull &&
      !satisfy_architecture(vnode.architecture, ArchitectureType::kAll, arch)) { continue; }
    frontier.emplace_back(vid);
  }
  return frontier;
}

DependencyTree WriteBuffer::query_dependency_tree(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  DependencyTree result;
  auto arch = graph_.architecture_types_.id(architecture).value_or(ArchitectureType::kNull);
  auto frontier = init_frontier(name, version, arch);
  std::unordered_set visited(frontier.begin(), frontier.end());
  auto expand_tree = [&, this, depth, arch]
  (const auto &self, VersionId vid, DependencyTree &tree, std::size_t level) -> void {
    for (auto did : version_nodes_[vid].dependencies) {
      const auto &dedge = dependency_edges_[did];
      const auto &pnode = package_nodes_[dedge.to_package];
      DependencyTree subtree;
      subtree.name = pnode.name;
      subtree.type = graph_.dependency_types_[dedge.type];
      subtree.version_constraint = dedge.version_constraint;
      subtree.architecture_constraint = graph_.architecture_types_[dedge.architecture_constraint];
      if (level + 1 < depth && (expand_alternative || dedge.group == 0))
        for (auto vid : pnode.versions) {
          const auto &vnode = version_nodes_[vid];
          if (visited.contains(vid)) continue;
          if (satisfy_architecture &&
            !xpg::satisfy_architecture(vnode.architecture, dedge.architecture_constraint, arch)) { continue; }
          if (satisfy_version && !xpg::satisfy_version(vnode.version, dedge.version_constraint)) continue;
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

DependencyFlat WriteBuffer::query_dependency_flat(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  DependencyFlat result(depth);
  auto arch = graph_.architecture_types_.id(architecture).value_or(ArchitectureType::kNull);
  auto frontier = init_frontier(name, version, arch);
  std::unordered_set visited(frontier.begin(), frontier.end());
  for (auto level : std::views::iota(0ull, depth)) {
    std::vector<VersionId> next;
    std::unordered_set<DependencyInfo> dvisited;
    for (auto vid : frontier) {
      std::vector<std::vector<DependencyInfo>> groups;
      std::vector<std::unordered_set<DependencyInfo>> gvisited;
      for (auto did : version_nodes_[vid].dependencies) {
        const auto &dedge = dependency_edges_[did];
        const auto &pnode = package_nodes_[dedge.to_package];
        DependencyInfo info;
        info.name = pnode.name;
        info.type = graph_.dependency_types_[dedge.type];
        info.version_constraint = dedge.version_constraint;
        info.architecture_constraint = graph_.architecture_types_[dedge.architecture_constraint];
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
        if (level + 1 < depth && (expand_alternative || dedge.group == 0))
          for (auto vid : pnode.versions) {
            const auto &vnode = version_nodes_[vid];
            if (visited.contains(vid)) continue;
            if (satisfy_architecture &&
              !xpg::satisfy_architecture(vnode.architecture, dedge.architecture_constraint, arch)) { continue; }
            if (satisfy_version && !xpg::satisfy_version(vnode.version, dedge.version_constraint)) continue;
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
