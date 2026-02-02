#include "storage_graph.hpp"
#include "buffer_graph.hpp"

StorageGraph::StorageGraph(std::size_t chunk_bytes) noexcept
  : control_(kSmallChunkBytes), architectures_(kSmallChunkBytes), dependency_types_(kSmallChunkBytes),
    package_nodes_(chunk_bytes), version_nodes_(chunk_bytes), dependency_edges_(chunk_bytes),
    version_list_nodes_(chunk_bytes), string_pool_(chunk_bytes), name_to_package_id_(0, string_pool_, string_pool_) {}

StorageGraph::StorageGraph(const std::filesystem::path &dir, open_mode mode,
                           std::initializer_list<std::string_view> archs,
                           std::initializer_list<std::string_view> dtypes, std::size_t chunk_bytes) noexcept
  : StorageGraph(chunk_bytes) {
  open(dir, mode, archs, dtypes);
}

bool StorageGraph::validate_control() const noexcept {
  if (control().magic != control_magic) return false;
  if (control().architecture_count != architecture_count()) return false;
  if (control().dependency_type_count != dependency_type_count()) return false;
  if (control().package_count != package_count()) return false;
  if (control().version_count != version_count()) return false;
  if (control().dependency_count != dependency_count()) return false;
  if (control().version_list_count != version_list_nodes_.size()) return false;
  if (control().string_pool_size != string_pool_.size_bytes()) return false;
  return true;
}

bool StorageGraph::load(const std::filesystem::path &dir) noexcept {
  close();
  if (!control_.load(dir.string() + "/.meta")) return false;
  if (control_.size() < control_size()) {
    control_.close();
    return false;
  }
  if (!architectures_.load(dir.string() + "/architectures.dat")) return false;
  if (!dependency_types_.load(dir.string() + "/dependency-types.dat")) return false;
  if (!package_nodes_.load(dir.string() + "/packages.dat")) return false;
  if (!version_nodes_.load(dir.string() + "/versions.dat")) return false;
  if (!dependency_edges_.load(dir.string() + "/dependencies.dat")) return false;
  if (!version_list_nodes_.load(dir.string() + "/version-lists.dat")) return false;
  if (!string_pool_.load(dir.string() + "/string-pool.dat")) return false;
  if (!validate_control()) return false;
  for (auto pid = static_cast<PackageId>(0); pid < package_count(); ++pid)
    name_to_package_id_.emplace(package_nodes_[pid].name, pid);
  return true;
}

bool StorageGraph::create(const std::filesystem::path &dir, std::initializer_list<std::string_view> archs,
                          std::initializer_list<std::string_view> dtypes) noexcept {
  close();
  if (!control_.create(dir.string() + "/.meta")) return false;
  control_.resize(control_size());
  if (!architectures_.create(dir.string() + "/architectures.dat", archs)) return false;
  if (!dependency_types_.create(dir.string() + "/dependency-types.dat", dtypes)) return false;
  if (!package_nodes_.create(dir.string() + "/packages.dat")) return false;
  if (!version_nodes_.create(dir.string() + "/versions.dat")) return false;
  if (!dependency_edges_.create(dir.string() + "/dependencies.dat")) return false;
  if (!version_list_nodes_.create(dir.string() + "/version-lists.dat")) return false;
  if (!string_pool_.create(dir.string() + "/string-pool.dat")) return false;
  control() = {
    .magic = control_magic,
    .architecture_count = architecture_count(),
    .dependency_type_count = dependency_type_count(),
    .package_count = package_count(),
    .version_count = version_count(),
    .dependency_count = dependency_count(),
    .version_list_count = version_list_nodes_.size(),
    .string_pool_size = string_pool_.size_bytes()
  };
  return true;
}

open_code StorageGraph::open(const std::filesystem::path &dir, open_mode mode,
                             std::initializer_list<std::string_view> archs,
                             std::initializer_list<std::string_view> dtypes) noexcept {
  switch (mode) {
  case open_mode::kLoad:
    if (load(dir)) return open_code::kLoadSuccess;
    return open_code::kOpenFailed;
  case open_mode::kCreate:
    if (create(dir, archs, dtypes)) return open_code::kCreateSuccess;
    return open_code::kOpenFailed;
  case open_mode::kLoadOrCreate:
    if (load(dir)) return open_code::kLoadSuccess;
    if (create(dir, archs, dtypes)) return open_code::kCreateSuccess;
    return open_code::kOpenFailed;
  default:
    return open_code::kOpenFailed;
  }
}

void StorageGraph::close() {
  control_.close();
  architectures_.close();
  dependency_types_.close();
  package_nodes_.close();
  version_nodes_.close();
  dependency_edges_.close();
  version_list_nodes_.close();
  string_pool_.close();
  name_to_package_id_.clear();
}

void StorageGraph::sync() {
  control_.sync();
  architectures_.sync();
  dependency_types_.sync();
  package_nodes_.sync();
  version_nodes_.sync();
  dependency_edges_.sync();
  version_list_nodes_.sync();
  string_pool_.sync();
}

void StorageGraph::set_chunk_bytes(std::size_t chunk_bytes) noexcept {
  package_nodes_.set_chunk_bytes(chunk_bytes);
  version_nodes_.set_chunk_bytes(chunk_bytes);
  dependency_edges_.set_chunk_bytes(chunk_bytes);
  version_list_nodes_.set_chunk_bytes(chunk_bytes);
  string_pool_.set_chunk_bytes(chunk_bytes);
}

ArchitectureId StorageGraph::intern_architecture(std::string_view arch) noexcept {
  auto id = architectures_.intern(arch);
  control().architecture_count = architecture_count();
  return id;
}

DependencyTypeId StorageGraph::intern_dependency_type(std::string_view dtype) noexcept {
  auto id = dependency_types_.intern(dtype);
  control().dependency_type_count = dependency_type_count();
  return id;
}

PackageView StorageGraph::get_package(PackageId pid) const noexcept {
  const auto &pnode = package_nodes_[pid];
  return {
    .name = string_pool_.get(pnode.name),
    .versions = [this, pid] {
      std::vector<VersionView> vviews;
      const auto &pnode = package_nodes_[pid];
      for (auto vlid = pnode.version_list; vlid != version_list_end;) {
        const auto &vlnode = version_list_nodes_[vlid];
        for (auto vid = vlnode.version_begin; vid < vlnode.version_begin + vlnode.version_count; ++vid)
          vviews.emplace_back(get_version(vid));
        vlid = vlnode.next;
      }
      return vviews;
    }
  };
}

VersionView StorageGraph::get_version(VersionId vid) const noexcept {
  const auto &vnode = version_nodes_[vid];
  return {
    .version = string_pool_.get(vnode.version),
    .architecture = architectures_.get(vnode.architecture),
    .dependencies = [this, vid] {
      std::vector<DependencyView> dviews;
      const auto &vnode = version_nodes_[vid];
      for (auto did = vnode.dependency_begin; did < vnode.dependency_begin + vnode.dependency_count; ++did)
        dviews.emplace_back(get_dependency(did));
      return dviews;
    }
  };
}

DependencyView StorageGraph::get_dependency(DependencyId did) const noexcept {
  const auto &dedge = dependency_edges_[did];
  return {
    .from_version = [this, did] { return get_version(dependency_edges_[did].from_version); },
    .to_package = [this, did] { return get_package(dependency_edges_[did].to_package); },
    .dependency_type = dependency_types_[dedge.dependency_type],
    .version_constraint = string_pool_.get(dedge.version_constraint),
    .architecture_constraint = architectures_[dedge.architecture_constraint],
    .group = dedge.group,
  };
}

std::optional<PackageView> StorageGraph::get_package(std::string_view name) const noexcept {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return get_package(it->second);
  return std::nullopt;
}

std::pair<PackageId, bool> StorageGraph::create_package(std::string_view name) {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return std::make_pair(it->second, false);
  auto pid = static_cast<PackageId>(package_count());
  auto offset = static_cast<StringOffset>(string_pool_.add(name));
  control().string_pool_size = string_pool_.size_bytes();
  package_nodes_.push_back({.name = offset, .version_list = version_list_end});
  name_to_package_id_.emplace(offset, pid);
  ++control().package_count;
  return std::make_pair(pid, true);
}

std::pair<VersionId, bool> StorageGraph::create_version(PackageId pid, std::string_view version, ArchitectureId arch) {
  const auto &pnode = package_nodes_[pid];
  for (auto vlid = pnode.version_list; vlid != version_list_end;) {
    const auto &vlnode = version_list_nodes_[vlid];
    for (auto vid = vlnode.version_begin; vid < vlnode.version_begin + vlnode.version_count; ++vid) {
      const auto &vnode = version_nodes_[vid];
      if (string_pool_.get(vnode.version) == version && vnode.architecture == arch) return std::make_pair(vid, false);
    }
    vlid = vlnode.next;
  }
  auto vid = static_cast<VersionId>(version_count());
  auto offset = static_cast<StringOffset>(string_pool_.add(version));
  control().string_pool_size = string_pool_.size_bytes();
  version_nodes_.push_back({
    .version = offset,
    .architecture = arch,
    .dependency_count = static_cast<DependencyCount>(0),
    .dependency_begin = static_cast<DependencyId>(dependency_count())
  });
  ++control().version_count;
  return std::make_pair(vid, true);
}

std::pair<DependencyId, bool> StorageGraph::create_dependency(
  VersionId from, PackageId to, std::string_view vcons, ArchitectureId acons, DependencyTypeId dtype, GroupId group) {
  auto did = static_cast<DependencyId>(dependency_count());
  auto offset = static_cast<StringOffset>(string_pool_.add(vcons));
  control().string_pool_size = string_pool_.size_bytes();
  dependency_edges_.push_back({
    .from_version = from,
    .to_package = to,
    .version_constraint = offset,
    .architecture_constraint = acons,
    .dependency_type = dtype,
    .group = group
  });
  ++control().dependency_count;
  return std::make_pair(did, true);
}

void StorageGraph::attach_versions(PackageId pid, VersionId vbegin, VersionCount vcount) {
  if (vcount == 0) return;
  auto &pnode = package_nodes_[pid];
  auto vlid = static_cast<VersionListId>(version_list_nodes_.size());
  version_list_nodes_.push_back({.version_count = vcount, .version_begin = vbegin, .next = pnode.version_list});
  pnode.version_list = vlid;
  ++control().version_list_count;
}

void StorageGraph::attach_dependencies(VersionId vid, DependencyId dbegin, DependencyCount dcount) {
  if (dcount == 0) return;
  auto &vnode = version_nodes_[vid];
  vnode.dependency_begin = dbegin;
  vnode.dependency_count = dcount;
}

void StorageGraph::ingest(const BufferGraph &bgraph) {
  for (const auto &bpnode : bgraph.package_nodes_) {
    auto vbegin = static_cast<VersionId>(version_count());
    auto vcount = static_cast<VersionCount>(0);
    auto [pid, psucc] = create_package(bpnode.name);

    for (auto bvid : bpnode.versions) {
      const auto &bvnode = bgraph.version_nodes_[bvid];
      auto dbegin = static_cast<DependencyId>(dependency_count());
      auto dcount = static_cast<DependencyCount>(0);
      auto [vid, vsucc] = create_version(pid, bvnode.version, bvnode.architecture);
      if (!vsucc) continue;
      ++vcount;

      for (auto bdid : bvnode.dependencies) {
        const auto &bdedge = bgraph.dependency_edges_[bdid];
        const auto &btpnode = bgraph.package_nodes_[bdedge.to_package];
        auto [tpid, tpsucc] = create_package(btpnode.name);
        create_dependency(vid, tpid, bdedge.version_constraint, bdedge.architecture_constraint, bdedge.dependency_type,
                          bdedge.group);
        ++dcount;
      }
      attach_dependencies(vid, dbegin, dcount);
    }
    attach_versions(pid, vbegin, vcount);
  }
}

bool StorageGraph::match_architecture(ArchitectureId origin, ArchitectureId target,
                                      ArchitectureId constr) const noexcept {
  if (architectures_[constr] == "native") return target == origin || architectures_[target] == "all";
  if (architectures_[constr] == "any") return true;
  return target == constr;
}

DependencyInfo StorageGraph::to_info(DependencyId did) const noexcept {
  const auto &dedge = dependency_edges_[did];
  const auto &tpnode = package_nodes_[dedge.to_package];
  return {
    .package_name = string_pool_.get(tpnode.name),
    .dependency_type = dependency_types_[dedge.dependency_type],
    .version_constraint = string_pool_.get(dedge.version_constraint),
    .architecture_constraint = architectures_[dedge.architecture_constraint]
  };
}

std::vector<VersionId> StorageGraph::init_frontier(std::string_view name, std::string_view version,
                                                   std::string_view arch) const {
  std::vector<VersionId> frontier;
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return frontier;
  const auto &pnode = package_nodes_[it->second];
  for (auto vlid = pnode.version_list; vlid != version_list_end;) {
    const auto &vlnode = version_list_nodes_[vlid];
    for (auto vid = vlnode.version_begin; vid < vlnode.version_begin + vlnode.version_count; ++vid) {
      const auto &vnode = version_nodes_[vid];
      if (!version.empty() && string_pool_.get(vnode.version) != version) continue;
      if (!arch.empty() && architectures_[vnode.architecture] != arch) continue;
      frontier.emplace_back(vid);
    }
    vlid = vlnode.next;
  }
  return frontier;
}

DependencyLevel StorageGraph::expand_frontier(std::vector<VersionId> &frontier, std::unordered_set<VersionId> &visited,
                                              bool has_next) const {
  DependencyLevel result;
  std::vector<VersionId> next;
  std::unordered_set<DependencyInfo> visited_dinfos;

  for (auto vid : frontier) {
    const auto &vnode = version_nodes_[vid];
    std::vector<DependencyGroup> groups;
    std::vector<std::unordered_set<DependencyInfo>> visited_ginfos;

    for (auto did = vnode.dependency_begin; did < vnode.dependency_begin + vnode.dependency_count; ++did) {
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

      if (has_next && dependency_types_[dedge.dependency_type] == "Depends" && dedge.group == 0)
        for (auto vlid = tpnode.version_list; vlid != version_list_end;) {
          const auto &vlnode = version_list_nodes_[vlid];
          for (auto nvid = vlnode.version_begin; nvid < vlnode.version_begin + vlnode.version_count; ++nvid) {
            if (visited.contains(nvid)) continue;
            const auto &nvnode = version_nodes_[nvid];
            if (match_architecture(vnode.architecture, nvnode.architecture, dedge.architecture_constraint)) {
              next.emplace_back(nvid);
              visited.emplace(nvid);
            }
          }
          vlid = vlnode.next;
        }
    }
    for (auto &group : groups) if (!group.empty()) result.or_dependencies.emplace_back(std::move(group));
  }
  frontier = std::move(next);
  return result;
}

DependencyResult StorageGraph::query_dependencies(std::vector<VersionId> &frontier, std::size_t depth) const {
  DependencyResult result(depth);
  if (frontier.empty()) return result;
  std::unordered_set visited(frontier.begin(), frontier.end());
  for (auto level = 0; level < depth; ++level) {
    result[level] = expand_frontier(frontier, visited, level + 1 < depth);
    if (frontier.empty()) break;
  }
  return result;
}
