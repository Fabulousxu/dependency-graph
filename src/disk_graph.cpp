#include "disk_graph.hpp"
#include "buffer_graph.hpp"

DiskGraph::DiskGraph(std::size_t chunk_bytes) noexcept
  : control_(kSmallChunkBytes), architectures_(kSmallChunkBytes), dependency_types_(kSmallChunkBytes),
    package_nodes_(chunk_bytes), version_nodes_(chunk_bytes), dependency_edges_(chunk_bytes),
    version_lists_(chunk_bytes), string_pool_(chunk_bytes), name_to_package_id_(0, string_pool_, string_pool_) {}

DiskGraph::DiskGraph(const std::filesystem::path &directory_path, open_mode mode,
                     std::initializer_list<std::string_view> architectures,
                     std::initializer_list<std::string_view> dependency_types, std::size_t chunk_bytes) noexcept
  : DiskGraph(chunk_bytes) {
  open(directory_path, mode, architectures, dependency_types);
}

bool DiskGraph::validate_control() const noexcept {
  if (control().magic != kMagicNumber) return false;
  if (control().architecture_count != architecture_count()) return false;
  if (control().dependency_type_count != dependency_type_count()) return false;
  if (control().package_count != package_count()) return false;
  if (control().version_count != version_count()) return false;
  if (control().dependency_count != dependency_count()) return false;
  if (control().version_list_count != version_lists_.size()) return false;
  if (control().string_pool_size != string_pool_.size()) return false;
  return true;
}

bool DiskGraph::load(const std::filesystem::path &directory_path) noexcept {
  using enum open_mode;
  using enum open_code;
  std::string dir = directory_path.string();
  if (control_.open(dir + "/.meta", kLoad) != kLoadSuccess) return false;
  if (control_.size() < control_size()) {
    control_.close();
    return false;
  }
  if (architectures_.open(dir + "/architectures.dat", kLoad) != kLoadSuccess) return false;
  if (dependency_types_.open(dir + "/dependency-types.dat", kLoad) != kLoadSuccess) return false;
  if (package_nodes_.open(dir + "/packages.dat", kLoad) != kLoadSuccess) return false;
  if (version_nodes_.open(dir + "/versions.dat", kLoad) != kLoadSuccess) return false;
  if (dependency_edges_.open(dir + "/dependencies.dat", kLoad) != kLoadSuccess) return false;
  if (version_lists_.open(dir + "/version-lists.dat", kLoad) != kLoadSuccess) return false;
  if (string_pool_.open(dir + "/string-pool.dat", kLoad) != kLoadSuccess) return false;
  if (!validate_control()) return false;

  for (PackageId pid = 0; pid < package_count(); ++pid) {
    const auto &pnode = package_nodes_[pid];
    string_handle handle{
      .offset = pnode.name_offset,
      .length = pnode.name_length
    };
    name_to_package_id_.emplace(handle, pid);
  }
  return true;
}

bool DiskGraph::create(const std::filesystem::path &directory_path,
                       std::initializer_list<std::string_view> architectures,
                       std::initializer_list<std::string_view> dependency_types) noexcept {
  using enum open_mode;
  using enum open_code;
  std::string dir = directory_path.string();
  if (control_.open(dir + "/.meta", kCreate) != kCreateSuccess) return false;
  control_.resize(control_size());
  if (architectures_.open(dir + "/architectures.dat", kCreate, architectures) != kCreateSuccess) return false;
  if (dependency_types_.open(dir + "/dependency-types.dat", kCreate, dependency_types) != kCreateSuccess) return false;
  if (package_nodes_.open(dir + "/packages.dat", kCreate) != kCreateSuccess) return false;
  if (version_nodes_.open(dir + "/versions.dat", kCreate) != kCreateSuccess) return false;
  if (dependency_edges_.open(dir + "/dependencies.dat", kCreate) != kCreateSuccess) return false;
  if (version_lists_.open(dir + "/version-lists.dat", kCreate) != kCreateSuccess) return false;
  if (string_pool_.open(dir + "/string-pool.dat", kCreate) != kCreateSuccess) return false;

  control().magic = kMagicNumber;
  control().architecture_count = architecture_count();
  control().dependency_type_count = dependency_type_count();
  control().package_count = 0;
  control().version_count = 0;
  control().dependency_count = 0;
  control().version_list_count = 0;
  control().string_pool_size = 0;
  return true;
}

open_code DiskGraph::open(const std::filesystem::path &directory_path, open_mode mode,
                          std::initializer_list<std::string_view> architectures,
                          std::initializer_list<std::string_view> dependency_types) noexcept {
  using enum open_mode;
  using enum open_code;
  if (mode == kLoad) {
    if (load(directory_path)) return kLoadSuccess;
    close();
    return kOpenFailed;
  }
  if (mode == kCreate) {
    if (create(directory_path, architectures, dependency_types)) return kCreateSuccess;
    close();
    return kOpenFailed;
  }
  if (mode == kLoadOrCreate) {
    if (load(directory_path)) return kLoadSuccess;
    if (create(directory_path, architectures, dependency_types)) return kCreateSuccess;
    close();
    return kOpenFailed;
  }
  return kOpenFailed;
}

void DiskGraph::close() {
  control_.close();
  architectures_.close();
  dependency_types_.close();
  package_nodes_.close();
  version_nodes_.close();
  dependency_edges_.close();
  version_lists_.close();
  string_pool_.close();
  name_to_package_id_.clear();
}

void DiskGraph::sync() {
  control_.sync();
  architectures_.sync();
  dependency_types_.sync();
  package_nodes_.sync();
  version_nodes_.sync();
  dependency_edges_.sync();
  version_lists_.sync();
  string_pool_.sync();
}

void DiskGraph::set_chunk_bytes(std::size_t chunk_bytes) noexcept {
  package_nodes_.set_chunk_bytes(chunk_bytes);
  version_nodes_.set_chunk_bytes(chunk_bytes);
  dependency_edges_.set_chunk_bytes(chunk_bytes);
  version_lists_.set_chunk_bytes(chunk_bytes);
  string_pool_.set_chunk_bytes(chunk_bytes);
}

ArchitectureType DiskGraph::add_architecture(std::string_view arch) noexcept {
  auto atype = architectures_.add(arch);
  control().architecture_count = architecture_count();
  return atype;
}

DependencyType DiskGraph::add_dependency_type(std::string_view dtype) noexcept {
  auto dtyp = dependency_types_.add(dtype);
  control().dependency_type_count = dependency_type_count();
  return dtyp;
}

PackageView DiskGraph::get_package(PackageId pid) const noexcept {
  const auto &pnode = package_nodes_[pid];
  return {
    .id = pid,
    .name = string_pool_.get(pnode.name_offset, pnode.name_length),
    .versions = [this, pid] {
      std::vector<VersionView> vviews;
      const auto &pnode = package_nodes_[pid];
      for (auto vlid = pnode.version_list_id; vlid != kVersionListEndId;) {
        const auto &vlnode = version_lists_[vlid];
        for (auto vid = vlnode.version_id_begin; vid < vlnode.version_id_begin + vlnode.version_count; ++vid)
          vviews.emplace_back(get_version(vid));
        vlid = vlnode.next_version_list_id;
      }
      return vviews;
    }
  };
}

VersionView DiskGraph::get_version(VersionId vid) const noexcept {
  const auto &vnode = version_nodes_[vid];
  return {
    .id = vid,
    .version = string_pool_.get(vnode.version_offset, vnode.version_length),
    .architecture = architectures_.get(vnode.architecture),
    .dependencies = [this, vid] {
      std::vector<DependencyView> dviews;
      const auto &vnode = version_nodes_[vid];
      for (auto did = vnode.dependency_id_begin; did < vnode.dependency_id_begin + vnode.dependency_count; ++did)
        dviews.emplace_back(get_dependency(did));
      return dviews;
    }
  };
}

DependencyView DiskGraph::get_dependency(DependencyId did) const noexcept {
  const auto &dedge = dependency_edges_[did];
  return {
    .id = did,
    .from_version = [this, did] { return get_version(dependency_edges_[did].from_version_id); },
    .to_package = [this, did] { return get_package(dependency_edges_[did].to_package_id); },
    .dependency_type = dependency_types_.get(dedge.dependency_type),
    .version_constraint = string_pool_.get(dedge.version_constraint_offset, dedge.version_constraint_length),
    .architecture_constraint = architectures_.get(dedge.architecture_constraint),
    .group = dedge.group,
  };
}

std::optional<PackageView> DiskGraph::get_package(std::string_view name) const noexcept {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return get_package(it->second);
  return std::nullopt;
}

std::pair<PackageId, bool> DiskGraph::create_package(std::string_view name) {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return {it->second, false};
  PackageId pid = package_count();
  auto handle = string_pool_.add(name);
  control().string_pool_size = string_pool_.size();

  package_nodes_.push_back({
    .name_offset = handle.offset,
    .name_length = handle.length,
    .version_list_id = kVersionListEndId
  });
  name_to_package_id_.emplace(handle, pid);
  control().package_count++;
  return {pid, true};
}

std::pair<VersionId, bool> DiskGraph::create_version(PackageId pid, std::string_view version, ArchitectureType arch,
                                                     DependencyId did_begin, DependencyCountType dcount) {
  const auto &pnode = package_nodes_[pid];
  for (auto vlid = pnode.version_list_id; vlid != kVersionListEndId;) {
    const auto &vlnode = version_lists_[vlid];
    for (auto vid = vlnode.version_id_begin; vid < vlnode.version_id_begin + vlnode.version_count; ++vid) {
      const auto &vnode = version_nodes_[vid];
      auto vstr = string_pool_.get(vnode.version_offset, vnode.version_length);
      if (vstr == version && vnode.architecture == arch) return {vid, false};
    }
    vlid = vlnode.next_version_list_id;
  }

  VersionId vid = version_count();
  auto handle = string_pool_.add(version);
  control().string_pool_size = string_pool_.size();

  version_nodes_.push_back({
    .version_offset = handle.offset,
    .version_length = handle.length,
    .architecture = arch,
    .dependency_count = dcount,
    .dependency_id_begin = did_begin
  });
  control().version_count++;
  return {vid, true};
}

std::pair<DependencyId, bool> DiskGraph::create_dependency(VersionId from_vid, PackageId to_pid, std::string_view vcons,
                                                           ArchitectureType acons, DependencyType dtype, GroupId gid) {
  DependencyId did = dependency_count();
  auto handle = string_pool_.add(vcons);
  control().string_pool_size = string_pool_.size();

  dependency_edges_.push_back({
    .from_version_id = from_vid,
    .to_package_id = to_pid,
    .version_constraint_offset = handle.offset,
    .version_constraint_length = handle.length,
    .architecture_constraint = acons,
    .dependency_type = dtype,
    .group = gid
  });
  control().dependency_count++;
  return {did, true};
}

void DiskGraph::attach_versions(PackageId pid, VersionId vid_begin, VersionCountType vcount) {
  if (vcount == 0) return;
  auto &pnode = package_nodes_[pid];
  VersionListId vlid = version_lists_.size();
  version_lists_.push_back({
    .version_count = vcount,
    .version_id_begin = vid_begin,
    .next_version_list_id = pnode.version_list_id
  });
  pnode.version_list_id = vlid;
  control().version_list_count++;
}

void DiskGraph::ingest(const BufferGraph &bgraph) {
  for (auto bpid = 0; bpid < bgraph.package_count(); ++bpid) {
    const auto &bpnode = bgraph.get_package(bpid);
    VersionId vid_begin = version_count();
    VersionCountType vcount = 0;
    auto [pid, psucc] = create_package(bpnode.name);

    for (auto bvid : bpnode.version_ids) {
      const auto &bvnode = bgraph.get_version(bvid);
      DependencyId did_begin = dependency_count();
      DependencyCountType dcount = bvnode.dependency_ids.size();
      auto [vid, vsucc] = create_version(pid, bvnode.version, bvnode.architecture, did_begin, dcount);
      if (!vsucc) continue;
      vcount++;

      for (auto bdid : bvnode.dependency_ids) {
        const auto &bdedge = bgraph.get_dependency(bdid);
        const auto &btpnode = bgraph.get_package(bdedge.to_package_id);
        auto [tpid, tpsucc] = create_package(btpnode.name);
        create_dependency(vid, tpid, bdedge.version_constraint, bdedge.architecture_constraint, bdedge.dependency_type,
                          bdedge.group);
      }
    }
    attach_versions(pid, vid_begin, vcount);
  }
}
