#include "storage_graph.hpp"
#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include "buffer_graph.hpp"

namespace xpg {

StorageGraph::StorageGraph(std::size_t growth_bytes) noexcept
  : control_(1024), architectures_(1024), dependency_types_(1024), package_nodes_(growth_bytes),
    version_ranges_(growth_bytes), version_nodes_(growth_bytes), dependency_edges_(growth_bytes),
    string_pool_(growth_bytes), name_to_package_id_(0, string_pool_, string_pool_) {}

StorageGraph::StorageGraph(const std::filesystem::path &directory, open_mode mode,
                           std::initializer_list<std::string_view> architectures,
                           std::initializer_list<std::string_view> dependency_types, std::size_t growth_bytes) noexcept
  : StorageGraph(growth_bytes) { open(directory, mode, architectures, dependency_types); }

void StorageGraph::load(const std::filesystem::path &directory) {
  close();
  control_.load(directory / "meta");
  architectures_.load(directory / "architectures");
  dependency_types_.load(directory / "dependency_types");
  package_nodes_.load(directory / "packages");
  version_ranges_.load(directory / "version_ranges");
  version_nodes_.load(directory / "versions");
  dependency_edges_.load(directory / "dependencies");
  string_pool_.load(directory / "string_pool");
  if (control_.size() < sizeof(Control) || control().magic != kMagic
    || control().architecture_count != architectures_.size()
    || control().dependency_type_count != dependency_types_.size()
    || control().package_count != package_nodes_.size()
    || control().version_range_count != version_ranges_.size()
    || control().version_count != version_nodes_.size()
    || control().dependency_count != dependency_edges_.size()
    || control().string_pool_size != string_pool_.size_bytes())
    throw std::runtime_error(std::format("Directory '{}' is not a valid XPackageGraph.", directory.string()));
  for (PackageId pid = 0; pid < package_nodes_.size(); ++pid)
    name_to_package_id_.emplace(package_nodes_[pid].name, pid);
}

void StorageGraph::create(const std::filesystem::path &directory, std::initializer_list<std::string_view> architectures,
                          std::initializer_list<std::string_view> dependency_types) {
  close();
  control_.create(directory / "meta");
  control_.resize(sizeof(Control));
  architectures_.create(directory / "architectures", architectures);
  dependency_types_.create(directory / "dependency_types", dependency_types);
  package_nodes_.create(directory / "packages");
  version_ranges_.create(directory / "version_ranges");
  version_nodes_.create(directory / "versions");
  dependency_edges_.create(directory / "dependencies");
  string_pool_.create(directory / "string_pool");
  control() = {
    kMagic, architectures_.size(), dependency_types_.size(), package_nodes_.size(), version_ranges_.size(),
    version_nodes_.size(), dependency_edges_.size(), string_pool_.size_bytes()
  };
}

void StorageGraph::open(const std::filesystem::path &directory, open_mode mode,
                        std::initializer_list<std::string_view> architectures,
                        std::initializer_list<std::string_view> dependency_types) {
  if (mode == open_mode::kLoad) load(directory);
  else if (mode == open_mode::kCreate) create(directory, architectures, dependency_types);
  else if (mode == open_mode::kLoadOrCreate)
    std::filesystem::exists(directory) ? load(directory) : create(directory, architectures, dependency_types);
  else throw std::invalid_argument("Invalid open_mode.");
}

void StorageGraph::close() {
  control_.close();
  architectures_.close();
  dependency_types_.close();
  package_nodes_.close();
  version_ranges_.close();
  version_nodes_.close();
  dependency_edges_.close();
  string_pool_.close();
  name_to_package_id_.clear();
}

void StorageGraph::sync() {
  control_.sync();
  architectures_.sync();
  dependency_types_.sync();
  package_nodes_.sync();
  version_ranges_.sync();
  version_nodes_.sync();
  dependency_edges_.sync();
  string_pool_.sync();
}

void StorageGraph::set_growth_bytes(std::size_t growth_bytes) noexcept {
  package_nodes_.set_growth_bytes(growth_bytes);
  version_ranges_.set_growth_bytes(growth_bytes);
  version_nodes_.set_growth_bytes(growth_bytes);
  dependency_edges_.set_growth_bytes(growth_bytes);
  string_pool_.set_growth_bytes(growth_bytes);
}

ArchitectureId StorageGraph::intern_architecture(std::string_view architecture) {
  auto id = architectures_.intern(architecture);
  control().architecture_count = architectures_.size();
  return id;
}

DependencyType StorageGraph::intern_dependency_type(std::string_view dependency_type) {
  auto id = dependency_types_.intern(dependency_type);
  control().dependency_type_count = dependency_types_.size();
  return id;
}

PackageView StorageGraph::get_package(PackageId pid) const noexcept {
  return {
    string_pool_[package_nodes_[pid].name], [this, pid] {
      std::vector<VersionView> vviews;
      for_each_version(pid, [this, &vviews](VersionId vid) { vviews.emplace_back(get_version(vid)); });
      return vviews;
    }
  };
}

VersionView StorageGraph::get_version(VersionId vid) const noexcept {
  return {
    string_pool_[version_nodes_[vid].version], architectures_[version_nodes_[vid].architecture],
    [this, vid] {
      std::vector<DependencyView> dviews;
      for_each_dependency(vid, [&, this](DependencyId did, const DependencyEdge &dedge) {
        if (dedge.group == 0) dviews.emplace_back(get_dependency(did));
      });
      return dviews;
    },
    [this, vid] {
      std::vector<std::vector<DependencyView>> gviews;
      for_each_dependency(vid, [&, this](DependencyId did, const DependencyEdge &dedge) {
        if (dedge.group == 0) return;
        if (gviews.size() < dedge.group) gviews.resize(dedge.group);
        gviews[dedge.group - 1].emplace_back(get_dependency(did));
      });
      std::erase_if(gviews, [](const auto &gview) { return gview.empty(); });
      return gviews;
    }
  };
}

DependencyView StorageGraph::get_dependency(DependencyId did) const noexcept {
  return {
    [this, did] { return get_version(dependency_edges_[did].from_version); },
    [this, did] { return get_package(dependency_edges_[did].to_package); },
    dependency_types_[dependency_edges_[did].type], string_pool_[dependency_edges_[did].version_constraint],
    architectures_[dependency_edges_[did].architecture_constraint],
  };
}

std::optional<PackageView> StorageGraph::get_package(std::string_view name) const noexcept {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return get_package(it->second);
  return std::nullopt;
}

bool StorageGraph::create_package(const PackageInfo &info, bool update_if_exists) {
  auto [pid, psucc] = create_package_node(info.name);
  auto arch = intern_architecture(info.architecture);
  auto [vid, vsucc] = create_version_node(pid, info.version, arch, update_if_exists);
  if (!vsucc) return false;
  auto dbegin = static_cast<DependencyId>(dependency_edges_.size());
  auto dcount = static_cast<DependencyCount>(0);
  auto create_dependency_edge_from_info = [this, vid, &dcount](const DependencyInfo &info, GroupId group) {
    auto [pid, pcuss] = create_package_node(info.name);
    auto arch = intern_architecture(info.architecture);
    auto type = intern_dependency_type(info.type);
    auto [did, dcuss] = create_dependency_edge(vid, pid, info.version_constraint, arch, type, group);
    if (dcuss) ++dcount;
  };
  for (const auto &dinfo : info.single_dependencies) create_dependency_edge_from_info(dinfo, 0);
  auto gid = static_cast<GroupId>(1);
  for (const auto &group : info.alternative_dependencies) {
    for (const auto &ginfo : group) create_dependency_edge_from_info(ginfo, gid);
    ++gid;
  }
  attach_dependencies(vid, dbegin, dcount);
  attach_versions(pid, vid, 1);
  return true;
}

bool StorageGraph::delete_package(std::string_view name, std::string_view version, std::string_view architecture) {
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return false;
  auto arch = architecture.empty() ? kNullArchitecture : architectures_.id(architecture).value_or(kNullArchitecture);
  bool deleted = false;
  for (auto vrid = package_nodes_[it->second].version_range; vrid != kVersionRangeEnd;) {
    auto &[version_count, version_begin, next] = version_ranges_[vrid];
    for (auto vid = version_begin; vid < version_begin + version_count;) {
      auto &vnode = version_nodes_[vid];
      if ((!version.empty() && string_pool_[vnode.version] != version)
        || (!architecture.empty() && vnode.architecture != arch)) {
        ++vid;
        continue;
      }
      deleted = true;
      --version_count;
      VersionId last = version_begin + version_count;
      if (vid == last) continue;
      vnode = version_nodes_[last];
      for_each_dependency(last, [vid](DependencyEdge &dedge) { dedge.from_version = vid; });
    }
    vrid = next;
  }
  return deleted;
}

std::pair<PackageId, bool> StorageGraph::create_package_node(std::string_view name) {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return {it->second, false};
  auto pid = static_cast<PackageId>(package_nodes_.size());
  auto nstr = static_cast<StringOffset>(string_pool_.append(name));
  control().string_pool_size = string_pool_.size_bytes();
  package_nodes_.push_back({nstr, kVersionRangeEnd});
  name_to_package_id_.emplace(nstr, pid);
  ++control().package_count;
  return {pid, true};
}

std::pair<VersionId, bool> StorageGraph::create_version_node(PackageId pid, std::string_view version,
                                                             ArchitectureId arch, bool update_if_exists) {
  bool exists = false;
  for (auto vrid = package_nodes_[pid].version_range; vrid != kVersionRangeEnd && !exists;) {
    auto &[version_count, version_begin, next] = version_ranges_[vrid];
    for (auto vid = version_begin; vid < version_begin + version_count; ++vid) {
      auto &vnode = version_nodes_[vid];
      if (string_pool_[vnode.version] != version || vnode.architecture != arch) continue;
      if (!update_if_exists) return {vid, false};
      exists = true;
      --version_count;
      VersionId last = version_begin + version_count;
      if (vid == last) break;
      vnode = version_nodes_[last];
      for_each_dependency(last, [vid](DependencyEdge &dedge) { dedge.from_version = vid; });
      break;
    }
    vrid = next;
  }
  auto vid = static_cast<VersionId>(version_nodes_.size());
  auto vstr = static_cast<StringOffset>(string_pool_.append(version));
  control().string_pool_size = string_pool_.size_bytes();
  version_nodes_.push_back({vstr, arch, 0, static_cast<DependencyId>(dependency_edges_.size())});
  ++control().version_count;
  return {vid, true};
}

std::pair<DependencyId, bool> StorageGraph::create_dependency_edge(VersionId from_vid, PackageId to_pid,
                                                                   std::string_view vcons, ArchitectureId acons,
                                                                   DependencyType type, GroupId group) {
  auto did = static_cast<DependencyId>(dependency_edges_.size());
  auto vcstr = static_cast<StringOffset>(string_pool_.append(vcons));
  control().string_pool_size = string_pool_.size_bytes();
  dependency_edges_.push_back({from_vid, to_pid, vcstr, acons, type, group});
  ++control().dependency_count;
  return {did, true};
}

void StorageGraph::attach_versions(PackageId pid, VersionId vbegin, VersionCount vcount) {
  if (vcount == 0) return;
  auto &pnode = package_nodes_[pid];
  auto vrid = static_cast<VersionRangeId>(version_ranges_.size());
  version_ranges_.push_back({vcount, vbegin, pnode.version_range});
  ++control().version_range_count;
  pnode.version_range = vrid;
}

void StorageGraph::attach_dependencies(VersionId vid, DependencyId dbegin, DependencyCount dcount) {
  if (dcount == 0) return;
  auto &vnode = version_nodes_[vid];
  vnode.dependency_begin = dbegin;
  vnode.dependency_count = dcount;
}

void StorageGraph::compact() {
  std::vector<VersionRange> vranges;
  std::vector<VersionNode> vnodes;
  std::vector<DependencyEdge> dedges;
  for (auto &pnode : package_nodes_) {
    auto vbegin = static_cast<VersionId>(vnodes.size());
    auto vcount = static_cast<VersionCount>(0);
    for_each_version(pnode, [&, this](const VersionNode &vnode) {
      auto dbegin = static_cast<DependencyId>(dedges.size());
      auto vid = static_cast<VersionId>(vnodes.size());
      vnodes.push_back({vnode.version, vnode.architecture, vnode.dependency_count, dbegin});
      for_each_dependency(vnode, [&, vid](const DependencyEdge &dedge) {
        dedges.push_back(
          {vid, dedge.to_package, dedge.version_constraint, dedge.architecture_constraint, dedge.type, dedge.group});
      });
      ++vcount;
    });
    if (vcount == 0) {
      pnode.version_range = kVersionRangeEnd;
      continue;
    }
    auto vrid = static_cast<VersionRangeId>(vranges.size());
    vranges.push_back({vcount, vbegin, kVersionRangeEnd});
    pnode.version_range = vrid;
  }
  version_ranges_.resize(vranges.size());
  std::ranges::copy(vranges, version_ranges_.begin());
  control().version_range_count = version_ranges_.size();
  version_nodes_.resize(vnodes.size());
  std::ranges::copy(vnodes, version_nodes_.begin());
  control().version_count = version_nodes_.size();
  dependency_edges_.resize(dedges.size());
  std::ranges::copy(dedges, dependency_edges_.begin());
  control().dependency_count = dependency_edges_.size();
}

std::vector<std::string_view> StorageGraph::query_packages(std::string_view architecture,
                                                           std::string_view prefix) const {
  std::vector<std::string_view> result;
  auto arch = architecture.empty() ? kNullArchitecture : architectures_.id(architecture).value_or(kNullArchitecture);
  for (const auto &pnode : package_nodes_) {
    auto name = string_pool_[pnode.name];
    if (!prefix.empty() && !name.starts_with(prefix)) continue;
    bool match = false;
    for_each_version(pnode, [&, arch](const VersionNode &vnode) {
      if (match) return;
      // if (!architecture.empty() && vnode.architecture != arch) return;
      if (!architecture.empty() && vnode.architecture != arch && vnode.architecture != kAllArchitecture) return;
      match = true;
    });
    if (match) result.emplace_back(name);
  }
  std::ranges::sort(result);
  return result;
}

std::vector<VersionInfo> StorageGraph::query_versions(std::string_view name, std::string_view architecture) const {
  std::vector<VersionInfo> versions;
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return versions;
  auto arch = architecture.empty() ? kNullArchitecture : architectures_.id(architecture).value_or(kNullArchitecture);
  for_each_version(it->second, [&, this](const VersionNode &vnode) {
    // if (!architecture.empty() && vnode.architecture != arch) return;
    if (!architecture.empty() && vnode.architecture != arch && vnode.architecture != kAllArchitecture) return;
    versions.push_back({string_pool_[vnode.version], architectures_[vnode.architecture]});
  });
  return versions;
}

std::variant<DependencyTree, DependencyFlat> StorageGraph::query_dependencies(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree) const {
  if (tree) return query_dependency_tree(name, version, architecture, depth);
  return query_dependency_flat(name, version, architecture, depth);
}

std::vector<VersionId> StorageGraph::init_frontier(std::string_view name, std::string_view version,
                                                   std::string_view architecture) const {
  std::vector<VersionId> frontier;
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return frontier;
  auto arch = architecture.empty() ? kNullArchitecture : architectures_.id(architecture).value_or(kNullArchitecture);
  for_each_version(it->second, [&, this](VersionId vid, const VersionNode &vnode) {
    if (!version.empty() && string_pool_[vnode.version] != version) return;
    if (!architecture.empty() && vnode.architecture != arch && vnode.architecture != kAllArchitecture) return;
    frontier.emplace_back(vid);
  });
  return frontier;
}

DependencyTree StorageGraph::query_dependency_tree(std::string_view name, std::string_view version,
                                                   std::string_view architecture, std::size_t depth) const {
  DependencyTree result{name, "", version, architecture};
  auto frontier = init_frontier(name, version, architecture);
  std::unordered_set visited(frontier.begin(), frontier.end());
  auto expand_tree = [&, this, depth]
  (const auto &self, VersionId vid, DependencyTree &tree, std::size_t level) -> void {
    for_each_dependency(vid, [&, this, depth, level](const DependencyEdge &dedge) {
      DependencyTree subtree{
        string_pool_[package_nodes_[dedge.to_package].name], dependency_types_[dedge.type],
        string_pool_[dedge.version_constraint], architectures_[dedge.architecture_constraint]
      };
      if (level + 1 < depth)
        for_each_version(package_nodes_[dedge.to_package], [&, level](VersionId vid, const VersionNode &vnode) {
          if (visited.contains(vid)) return;
          if (!match_architecture(vnode.architecture, dedge.architecture_constraint)) return;
          visited.insert(vid);
          self(self, vid, subtree, level + 1);
        });
      if (dedge.group != 0) {
        if (tree.alternative_dependencies.size() < dedge.group) tree.alternative_dependencies.resize(dedge.group);
        tree.alternative_dependencies[dedge.group - 1].emplace_back(std::move(subtree));
      } else tree.single_dependencies.emplace_back(std::move(subtree));
    });
  };
  for (auto vid : frontier) expand_tree(expand_tree, vid, result, 0);
  return result;
}

DependencyFlat StorageGraph::query_dependency_flat(std::string_view name, std::string_view version,
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
      for_each_dependency(vid, [&, this, depth, level](const DependencyEdge &dedge) {
        DependencyInfo info{
          string_pool_[package_nodes_[dedge.to_package].name], dependency_types_[dedge.type],
          string_pool_[dedge.version_constraint], architectures_[dedge.architecture_constraint]
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
          for_each_version(package_nodes_[dedge.to_package], [&](VersionId vid, const VersionNode &vnode) {
            if (visited.contains(vid)) return;
            if (!match_architecture(vnode.architecture, dedge.architecture_constraint)) return;
            next.emplace_back(vid);
            visited.emplace(vid);
          });
      });
      for (auto &group : groups)
        if (!group.empty()) result[level].alternative_dependencies.emplace_back(std::move(group));
    }
    frontier = std::move(next);
    if (frontier.empty()) break;
  }
  return result;
}

} // namespace xpg
