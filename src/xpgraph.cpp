#include "xpgraph.hpp"
#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>

namespace xpg {

XPGraph::XPGraph(std::size_t flush_limit_bytes, std::size_t growth_bytes) noexcept
  : meta_(1_KB), architecture_types_(1_KB), dependency_types_(1_KB), package_nodes_(growth_bytes),
    version_ranges_(growth_bytes), version_nodes_(growth_bytes), dependency_edges_(growth_bytes),
    string_pool_(growth_bytes), name_to_package_id_(0, string_pool_, string_pool_), write_buffer_(*this),
    cuda_cache_(*this), flush_limit_bytes_(flush_limit_bytes) {}

XPGraph::XPGraph(
  const std::filesystem::path &directory, open_mode mode, std::span<const std::string_view> architecture_types,
  std::span<const std::string_view> dependency_types, std::size_t flush_limit_bytes, std::size_t growth_bytes)
  : XPGraph(flush_limit_bytes, growth_bytes) { open(directory, mode, architecture_types, dependency_types); }

XPGraph::XPGraph(
  const std::filesystem::path &directory, open_mode mode, std::initializer_list<std::string_view> architecture_types,
  std::initializer_list<std::string_view> dependency_types, std::size_t flush_limit_bytes, std::size_t growth_bytes)
  : XPGraph(directory, mode, std::span(architecture_types), std::span(dependency_types), flush_limit_bytes,
            growth_bytes) {}

void XPGraph::load(const std::filesystem::path &directory) {
  close();
  meta_.load(directory / "meta");
  architecture_types_.load(directory / "architecture_types");
  dependency_types_.load(directory / "dependency_types");
  package_nodes_.load(directory / "package_nodes");
  version_ranges_.load(directory / "version_ranges");
  version_nodes_.load(directory / "version_nodes");
  dependency_edges_.load(directory / "dependency_edges");
  string_pool_.load(directory / "string_pool");
  if (meta_.size() < sizeof(Meta) || meta().magic != kMagic || meta().architecture_types != architecture_types_.size()
    || meta().dependency_types != dependency_types_.size() || meta().package_count != package_nodes_.size()
    || meta().version_ranges != version_ranges_.size() || meta().version_count != version_nodes_.size()
    || meta().dependency_count != dependency_edges_.size() || meta().string_pool_size != string_pool_.size_bytes())
    throw std::runtime_error(std::format("Directory '{}' is not a valid XPGraph data directory.", directory.string()));
  for (PackageId pid = 0; pid < package_nodes_.size(); ++pid)
    name_to_package_id_.emplace(package_nodes_[pid].name, pid);
}

void XPGraph::create(const std::filesystem::path &directory, std::span<const std::string_view> architecture_types,
                     std::span<const std::string_view> dependency_types) {
  close();
  meta_.create(directory / "meta");
  meta_.resize(sizeof(Meta));
  architecture_types_.create(directory / "architecture_types", architecture_types);
  dependency_types_.create(directory / "dependency_types", dependency_types);
  package_nodes_.create(directory / "package_nodes");
  version_ranges_.create(directory / "version_ranges");
  version_nodes_.create(directory / "version_nodes");
  dependency_edges_.create(directory / "dependency_edges");
  string_pool_.create(directory / "string_pool");
  meta().magic = kMagic;
  meta().architecture_types = architecture_types_.size();
  meta().dependency_types = dependency_types_.size();
  meta().package_count = package_nodes_.size();
  meta().version_ranges = version_ranges_.size();
  meta().version_count = version_nodes_.size();
  meta().dependency_count = dependency_edges_.size();
  meta().string_pool_size = string_pool_.size_bytes();
}

void XPGraph::create(const std::filesystem::path &directory, std::initializer_list<std::string_view> architecture_types,
                     std::initializer_list<std::string_view> dependency_types) {
  create(directory, std::span(architecture_types), std::span(dependency_types));
}

void XPGraph::open(const std::filesystem::path &directory, open_mode mode,
                   std::span<const std::string_view> architecture_types,
                   std::span<const std::string_view> dependency_types) {
  if (mode == open_mode::kLoad) load(directory);
  else if (mode == open_mode::kCreate) create(directory, architecture_types, dependency_types);
  else if (mode == open_mode::kLoadOrCreate)
    std::filesystem::exists(directory) ? load(directory) : create(directory, architecture_types, dependency_types);
  else throw std::invalid_argument("Invalid open_mode.");
}

void XPGraph::open(const std::filesystem::path &directory, open_mode mode,
                   std::initializer_list<std::string_view> architecture_types,
                   std::initializer_list<std::string_view> dependency_types) {
  open(directory, mode, std::span(architecture_types), std::span(dependency_types));
}

void XPGraph::sync() {
  meta_.sync();
  architecture_types_.sync();
  dependency_types_.sync();
  package_nodes_.sync();
  version_ranges_.sync();
  version_nodes_.sync();
  dependency_edges_.sync();
  string_pool_.sync();
}

void XPGraph::close() {
  clear_buffer();
  clear_cache();
  meta_.close();
  architecture_types_.close();
  dependency_types_.close();
  package_nodes_.close();
  version_ranges_.close();
  version_nodes_.close();
  dependency_edges_.close();
  string_pool_.close();
  name_to_package_id_.clear();
}

void XPGraph::set_growth_bytes(std::size_t growth_bytes) noexcept {
  package_nodes_.set_growth_bytes(growth_bytes);
  version_ranges_.set_growth_bytes(growth_bytes);
  version_nodes_.set_growth_bytes(growth_bytes);
  dependency_edges_.set_growth_bytes(growth_bytes);
  string_pool_.set_growth_bytes(growth_bytes);
}

std::size_t XPGraph::estimated_memory_usage() const noexcept {
  return sizeof(XPGraph) + write_buffer_.estimated_memory_usage() - sizeof(WriteBuffer);
}

bool XPGraph::flush_buffer_if_needed(bool update_if_exists) {
  auto needed = estimated_memory_usage() >= flush_limit_bytes_;
  if (needed) flush_buffer(update_if_exists);
  return needed;
}

void XPGraph::compact() {
  clear_cache();
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
  meta().version_ranges = version_ranges_.size();
  version_nodes_.resize(vnodes.size());
  std::ranges::copy(vnodes, version_nodes_.begin());
  meta().version_count = version_nodes_.size();
  dependency_edges_.resize(dedges.size());
  std::ranges::copy(dedges, dependency_edges_.begin());
  meta().dependency_count = dependency_edges_.size();
}

const symbol_table<ArchitectureType, StringLength> &XPGraph::architecture_types() const noexcept {
  return architecture_types_;
}

const symbol_table<DependencyType, StringLength> &XPGraph::dependency_types() const noexcept {
  return dependency_types_;
}

ArchitectureType XPGraph::intern_architecture(std::string_view architecture) {
  auto id = architecture_types_.intern(architecture);
  meta().architecture_types = architecture_types_.size();
  return id;
}

DependencyType XPGraph::intern_dependency(std::string_view dependency) {
  auto id = dependency_types_.intern(dependency);
  meta().dependency_types = dependency_types_.size();
  return id;
}

PackageView XPGraph::get_package(PackageId pid) const noexcept {
  PackageView pview;
  const auto &pnode = package_nodes_[pid];
  pview.name = string_pool_[pnode.name];
  pview.versions = [this, pid] {
    std::vector<VersionView> vviews;
    for_each_version(pid, [this, &vviews](VersionId vid) { vviews.emplace_back(get_version(vid)); });
    return vviews;
  };
  return pview;
}

std::optional<PackageView> XPGraph::get_package(std::string_view name) const noexcept {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return get_package(it->second);
  return std::nullopt;
}

VersionView XPGraph::get_version(VersionId vid) const noexcept {
  VersionView vview;
  const auto &vnode = version_nodes_[vid];
  vview.version = string_pool_[vnode.version];
  vview.architecture = architecture_types_[vnode.architecture];
  vview.single_dependencies = [this, vid] {
    std::vector<DependencyView> dviews;
    for_each_dependency(vid, [&, this](DependencyId did, const DependencyEdge &dedge) {
      if (dedge.group == 0) dviews.emplace_back(get_dependency(did));
    });
    return dviews;
  };
  vview.alternative_dependencies = [this, vid] {
    std::vector<std::vector<DependencyView>> gviews;
    for_each_dependency(vid, [&, this](DependencyId did, const DependencyEdge &dedge) {
      if (dedge.group == 0) return;
      if (gviews.size() < dedge.group) gviews.resize(dedge.group);
      gviews[dedge.group - 1].emplace_back(get_dependency(did));
    });
    std::erase_if(gviews, [](const auto &gview) { return gview.empty(); });
    return gviews;
  };
  return vview;
}

DependencyView XPGraph::get_dependency(DependencyId did) const noexcept {
  DependencyView dview;
  const auto &dedge = dependency_edges_[did];
  dview.from_version = [this, did] { return get_version(dependency_edges_[did].from_version); };
  dview.to_package = [this, did] { return get_package(dependency_edges_[did].to_package); };
  dview.type = dependency_types_[dedge.type];
  dview.version_constraint = string_pool_[dedge.version_constraint];
  dview.architecture_constraint = architecture_types_[dedge.architecture_constraint];
  return dview;
}

bool XPGraph::create_package(const PackageInfo &info, bool update_if_exists) {
  auto [pid, psucc] = create_package_node(info.name);
  auto arch = intern_architecture(info.architecture);
  auto [vid, vsucc] = create_version_node(pid, info.version, arch, update_if_exists);
  if (!vsucc) return false;
  auto dbegin = static_cast<DependencyId>(dependency_edges_.size());
  auto dcount = static_cast<DependencyCount>(0);
  auto create_dependency_edge_from_info = [this, vid, &dcount](const DependencyInfo &info, DependencyGroupId group) {
    auto [pid, pcuss] = create_package_node(info.name);
    auto arch = intern_architecture(info.architecture_constraint);
    auto type = intern_dependency(info.type);
    auto [did, dcuss] = create_dependency_edge(vid, pid, info.version_constraint, arch, type, group);
    if (dcuss) ++dcount;
  };
  for (const auto &dinfo : info.single_dependencies) create_dependency_edge_from_info(dinfo, 0);
  auto gid = static_cast<DependencyGroupId>(1);
  for (const auto &group : info.alternative_dependencies) {
    for (const auto &ginfo : group) create_dependency_edge_from_info(ginfo, gid);
    ++gid;
  }
  attach_dependencies(vid, dbegin, dcount);
  attach_versions(pid, vid, 1);
  return true;
}

std::pair<PackageId, bool> XPGraph::create_package_node(std::string_view name) {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return {it->second, false};
  auto pid = static_cast<PackageId>(package_nodes_.size());
  auto nstr = static_cast<StringId>(string_pool_.append(name));
  meta().string_pool_size = string_pool_.size_bytes();
  auto &pnode = package_nodes_.emplace_back();
  pnode.name = nstr;
  pnode.version_range = kVersionRangeEnd;
  name_to_package_id_.emplace(nstr, pid);
  ++meta().package_count;
  return {pid, true};
}

std::pair<VersionId, bool> XPGraph::create_version_node(PackageId pid, std::string_view version,
                                                        ArchitectureType arch, bool update_if_exists) {
  bool exists = false;
  for (auto vrid = package_nodes_[pid].version_range; vrid != kVersionRangeEnd && !exists;) {
    auto &vrange = version_ranges_[vrid];
    for (auto vid = vrange.version_begin; vid < vrange.version_begin + vrange.version_count; ++vid) {
      auto &vnode = version_nodes_[vid];
      if (string_pool_[vnode.version] != version || vnode.architecture != arch) continue;
      if (!update_if_exists) return {vid, false};
      exists = true;
      --vrange.version_count;
      VersionId last = vrange.version_begin + vrange.version_count;
      if (vid == last) break;
      vnode = version_nodes_[last];
      for_each_dependency(last, [vid](DependencyEdge &dedge) { dedge.from_version = vid; });
      break;
    }
    vrid = vrange.next;
  }
  auto vid = static_cast<VersionId>(version_nodes_.size());
  auto vstr = static_cast<StringId>(string_pool_.append(version));
  meta().string_pool_size = string_pool_.size_bytes();
  auto &vnode = version_nodes_.emplace_back();
  vnode.version = vstr;
  vnode.architecture = arch;
  vnode.dependency_begin = static_cast<DependencyId>(dependency_edges_.size());
  vnode.dependency_count = 0;
  ++meta().version_count;
  return {vid, true};
}

std::pair<DependencyId, bool> XPGraph::create_dependency_edge(
  VersionId from_vid, PackageId to_pid, std::string_view vcons, ArchitectureType acons, DependencyType type,
  DependencyGroupId group) {
  auto did = static_cast<DependencyId>(dependency_edges_.size());
  auto vcstr = static_cast<StringId>(string_pool_.append(vcons));
  meta().string_pool_size = string_pool_.size_bytes();
  auto &dedge = dependency_edges_.emplace_back();
  dedge.from_version = from_vid;
  dedge.to_package = to_pid;
  dedge.type = type;
  dedge.version_constraint = vcstr;
  dedge.architecture_constraint = acons;
  dedge.group = group;
  ++meta().dependency_count;
  return {did, true};
}

bool XPGraph::delete_package(std::string_view name, std::string_view version, std::string_view architecture) {
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return false;
  auto arch = architecture_types_.id(architecture).value_or(ArchitectureType::kNull);
  bool deleted = false;
  for (auto vrid = package_nodes_[it->second].version_range; vrid != kVersionRangeEnd;) {
    auto &vrange = version_ranges_[vrid];
    for (auto vid = vrange.version_begin; vid < vrange.version_begin + vrange.version_count;) {
      auto &vnode = version_nodes_[vid];
      if ((!version.empty() && string_pool_[vnode.version] != version) ||
        (!architecture.empty() && vnode.architecture != arch)) {
        ++vid;
        continue;
      }
      deleted = true;
      --vrange.version_count;
      VersionId last = vrange.version_begin + vrange.version_count;
      if (vid == last) continue;
      vnode = version_nodes_[last];
      for_each_dependency(last, [vid](DependencyEdge &dedge) { dedge.from_version = vid; });
    }
    vrid = vrange.next;
  }
  return deleted;
}

void XPGraph::attach_versions(PackageId pid, VersionId vbegin, VersionCount vcount) {
  if (vcount == 0) return;
  auto &pnode = package_nodes_[pid];
  auto vrid = static_cast<VersionRangeId>(version_ranges_.size());
  auto &vrange = version_ranges_.emplace_back();
  vrange.version_begin = vbegin;
  vrange.version_count = vcount;
  vrange.next = pnode.version_range;
  ++meta().version_ranges;
  pnode.version_range = vrid;
}

void XPGraph::attach_dependencies(VersionId vid, DependencyId dbegin, DependencyCount dcount) {
  if (dcount == 0) return;
  auto &vnode = version_nodes_[vid];
  vnode.dependency_begin = dbegin;
  vnode.dependency_count = dcount;
}

PackageView XPGraph::get_package_in_buffer(PackageId pid) const noexcept { return write_buffer_.get_package(pid); }

std::optional<PackageView> XPGraph::get_package_in_buffer(std::string_view name) const noexcept {
  return write_buffer_.get_package(name);
}

bool XPGraph::create_package_in_buffer(const PackageInfo &info, bool update_if_exists) {
  return write_buffer_.create_package(info, update_if_exists);
}

std::vector<std::string_view> XPGraph::query_packages(std::string_view architecture, std::string_view prefix) const {
  std::vector<std::string_view> result;
  auto arch = architecture_types_.id(architecture).value_or(ArchitectureType::kNull);
  for (const auto &pnode : package_nodes_) {
    auto name = string_pool_[pnode.name];
    if (!prefix.empty() && !name.starts_with(prefix)) continue;
    bool match = false;
    for_each_version(pnode, [&, arch](const VersionNode &vnode) {
      if (match) return;
      if (arch != ArchitectureType::kNull && !satisfy_architecture(vnode.architecture, ArchitectureType::kAll, arch))
        return;
      match = true;
    });
    if (match) result.emplace_back(name);
  }
  std::ranges::sort(result);
  return result;
}

std::vector<VersionInfo> XPGraph::query_versions(std::string_view name, std::string_view architecture) const {
  std::vector<VersionInfo> versions;
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return versions;
  auto arch = architecture_types_.id(architecture).value_or(ArchitectureType::kNull);
  for_each_version(it->second, [&, this, arch](const VersionNode &vnode) {
    if (arch != ArchitectureType::kNull && !satisfy_architecture(vnode.architecture, ArchitectureType::kAll, arch))
      return;
    versions.push_back({string_pool_[vnode.version], architecture_types_[vnode.architecture]});
  });
  return versions;
}

std::variant<DependencyTree, DependencyFlat> XPGraph::query_dependencies(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool tree, bool use_gpu, bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  if (depth == 1) tree = false;
  if (!use_gpu) {
    if (tree)
      return query_dependency_tree(name, version, architecture, depth, satisfy_architecture, satisfy_version,
                                   expand_alternative);
    return query_dependency_flat(name, version, architecture, depth, satisfy_architecture, satisfy_version,
                                 expand_alternative);
  }
  if (!cuda_cache_.is_built()) cuda_cache_.build();
  return cuda_cache_.query_dependencies(name, version, architecture, depth, tree, satisfy_architecture,
                                        satisfy_version, expand_alternative);
}

std::vector<VersionId> XPGraph::init_frontier(std::string_view name, std::string_view version,
                                              ArchitectureType arch) const {
  std::vector<VersionId> frontier;
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return frontier;
  for_each_version(it->second, [&, this, arch](VersionId vid, const VersionNode &vnode) {
    if (!version.empty() && string_pool_[vnode.version] != version) return;
    if (arch != ArchitectureType::kNull && !satisfy_architecture(vnode.architecture, ArchitectureType::kAll, arch))
      return;
    frontier.emplace_back(vid);
  });
  return frontier;
}

DependencyTree XPGraph::query_dependency_tree(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  DependencyTree result;
  result.name = name;
  result.version_constraint = version;
  result.architecture_constraint = architecture;
  auto arch = architecture_types_.id(architecture).value_or(ArchitectureType::kNull);
  auto frontier = init_frontier(name, version, arch);
  std::unordered_set visited(frontier.begin(), frontier.end());
  auto expand_tree = [&, this, depth, arch]
  (const auto &self, VersionId vid, DependencyTree &tree, std::size_t level) -> void {
    for_each_dependency(vid, [&, this, depth, level, arch](const DependencyEdge &dedge) {
      const auto &pnode = package_nodes_[dedge.to_package];
      DependencyTree subtree;
      subtree.name = string_pool_[pnode.name];
      subtree.type = dependency_types_[dedge.type];
      subtree.version_constraint = string_pool_[dedge.version_constraint];
      subtree.architecture_constraint = architecture_types_[dedge.architecture_constraint];
      if (level + 1 < depth && (expand_alternative || dedge.group == 0))
        for_each_version(pnode, [&, level, arch](VersionId vid, const VersionNode &vnode) {
          if (visited.contains(vid)) return;
          if (satisfy_architecture &&
            !xpg::satisfy_architecture(vnode.architecture, dedge.architecture_constraint, arch)) { return; }
          if (satisfy_version &&
            !xpg::satisfy_version(string_pool_[vnode.version], string_pool_[dedge.version_constraint])) { return; }
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

DependencyFlat XPGraph::query_dependency_flat(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  DependencyFlat result(depth);
  auto arch = architecture_types_.id(architecture).value_or(ArchitectureType::kNull);
  auto frontier = init_frontier(name, version, arch);
  std::unordered_set visited(frontier.begin(), frontier.end());
  for (auto level : std::views::iota(0ull, depth)) {
    std::vector<VersionId> next;
    std::unordered_set<DependencyInfo> dvisited;
    for (auto vid : frontier) {
      std::vector<std::vector<DependencyInfo>> groups;
      std::vector<std::unordered_set<DependencyInfo>> gvisited;
      for_each_dependency(vid, [&, this, depth, level, arch](const DependencyEdge &dedge) {
        const auto &pnode = package_nodes_[dedge.to_package];
        DependencyInfo info;
        info.name = string_pool_[pnode.name];
        info.type = dependency_types_[dedge.type];
        info.version_constraint = string_pool_[dedge.version_constraint];
        info.architecture_constraint = architecture_types_[dedge.architecture_constraint];
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
          for_each_version(pnode, [&, arch](VersionId vid, const VersionNode &vnode) {
            if (visited.contains(vid)) return;
            if (satisfy_architecture &&
              !xpg::satisfy_architecture(vnode.architecture, dedge.architecture_constraint, arch)) { return; }
            if (satisfy_version &&
              !xpg::satisfy_version(string_pool_[vnode.version], string_pool_[dedge.version_constraint])) { return; }
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

std::variant<DependencyTree, DependencyFlat> XPGraph::query_dependencies_in_buffer(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  if (depth == 1) tree = false;
  return write_buffer_.query_dependencies(name, version, architecture, depth, tree, satisfy_architecture,
                                          satisfy_version, expand_alternative);
}

} // namespace xpg
