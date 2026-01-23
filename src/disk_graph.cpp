#include "disk_graph.hpp"
#include "buffer_graph.hpp"

#include <iostream>

DiskGraph::DiskGraph(const std::filesystem::path &directory_path, OpenMode mode,
  std::initializer_list<std::string_view> architectures, std::initializer_list<std::string_view> dependency_types,
  std::size_t chunk_bytes)
  : control_file_{kSmallChunkBytes}, package_nodes_{chunk_bytes}, version_nodes_{chunk_bytes},
    dependency_edges_{chunk_bytes}, version_lists_{chunk_bytes}, architectures_{kSmallChunkBytes},
    dependency_types_{kSmallChunkBytes}, string_pool_{chunk_bytes},
    name_to_package_id_{0, StringHandleHash<false>{string_pool_}, StringHandleEqual<false>{string_pool_}} {
  std::unordered_set<OpenCode> ocs;
  ocs.emplace(control_file_.open(directory_path.string() + "/.meta", mode));
  ocs.emplace(package_nodes_.open(directory_path.string() + "/packages.dat", mode));
  ocs.emplace(version_nodes_.open(directory_path.string() + "/versions.dat", mode));
  ocs.emplace(dependency_edges_.open(directory_path.string() + "/dependencies.dat", mode));
  ocs.emplace(version_lists_.open(directory_path.string() + "/verlist.dat", mode));
  ocs.emplace(string_pool_.open(directory_path.string() + "/strings.dat", mode));
  ocs.emplace(architectures_.open(directory_path.string() + "/archs.dat", mode, architectures));
  ocs.emplace(dependency_types_.open(directory_path.string() + "/dtypes.dat", mode, dependency_types));
  if (ocs.size() > 1) throw std::ios_base::failure{"Inconsistent Open Codes"};

  switch (*ocs.begin()) {
  case kLoadSuccess:
    if (control_file_.size() < control_size()) throw std::ios_base::failure{"Corrupted Control File"};
    if (!validate_control()) throw std::ios_base::failure{"Invalid Control Data"};
    for (PackageId pid = 0; pid < package_count(); pid++) {
      const auto &pnode = package_nodes_[pid];
      StringHandle handle;
      handle.length = pnode.name_length;
      handle.offset = pnode.name_offset;
      name_to_package_id_.emplace(handle, pid);
    }

    break;
  case kCreateSuccess:
    control_file_.resize(control_size());
    control().magic = kMagicNumber;
    control().package_count = 0;
    control().version_count = 0;
    control().dependency_count = 0;
    control().version_list_count = 0;
    control().architecture_count = architectures_.size();
    control().dependency_type_count = dependency_types_.size();
    control().string_pool_size = 0;
    break;
  default:
    throw std::ios_base::failure{"Open Failed"};
  }
}

bool DiskGraph::validate_control() const noexcept {
  if (control().magic != kMagicNumber) return false;
  if (control().package_count != package_nodes_.size()) return false;
  if (control().version_count != version_nodes_.size()) return false;
  if (control().dependency_count != dependency_edges_.size()) return false;
  if (control().version_list_count != version_lists_.size()) return false;
  if (control().architecture_count != architectures_.size()) return false;
  if (control().dependency_type_count != dependency_types_.size()) return false;
  if (control().string_pool_size != string_pool_.size()) return false;
  return true;
}

PackageView DiskGraph::get_package(PackageId pid) const {
  PackageView pview;
  const auto &pnode = package_nodes_[pid];
  pview.id = pid;
  pview.name = string_pool_.get({pnode.name_offset, pnode.name_length});
  pview.versions = [this, pid] {
    std::vector<VersionView> vviews;
    const auto &pnode = package_nodes_[pid];
    for (auto vlid = pnode.version_list_id; vlid != kVersionListEndId;) {
      const auto &vlnode = version_lists_[vlid];
      for (auto vid = vlnode.version_id_begin; vid < vlnode.version_id_begin + vlnode.version_count; vid++)
        vviews.emplace_back(get_version(vid));
      vlid = vlnode.next_version_list_id;
    }
    return vviews;
  };
  return pview;
}

VersionView DiskGraph::get_version(VersionId vid) const {
  VersionView vview;
  const auto &vnode = version_nodes_[vid];
  vview.id = vid;
  vview.version = string_pool_.get({vnode.version_offset, vnode.version_length});
  vview.architecture = architectures_.get(vnode.architecture);
  vview.dependencies = [this, vid] {
    std::vector<DependencyView> dviews;
    const auto &vnode = version_nodes_[vid];
    for (auto did = vnode.dependency_id_begin; did < vnode.dependency_id_begin + vnode.dependency_count; did++)
      dviews.emplace_back(get_dependency(did));
    return dviews;
  };
  return vview;
}

DependencyView DiskGraph::get_dependency(DependencyId did) const {
  DependencyView dview;
  const auto &dedge = dependency_edges_[did];
  dview.id = did;
  dview.from_version = [this, did] { return get_version(dependency_edges_[did].from_version_id); };
  dview.to_package = [this, did] { return get_package(dependency_edges_[did].to_package_id); };
  dview.version_constraint = string_pool_.get({dedge.version_constraint_offset, dedge.version_constraint_length});
  dview.architecture_constraint = architectures_.get(dedge.architecture_constraint);
  dview.dependency_type = dependency_types_.get(dedge.dependency_type);
  dview.group = static_cast<std::size_t>(dedge.group);
  return dview;
}

std::pair<PackageId, bool> DiskGraph::create_package(std::string_view name) {
  auto it = name_to_package_id_.find(name);
  if (it != name_to_package_id_.end()) return {it->second, false};
  PackageId pid = package_count();
  auto &pnode = package_nodes_.emplace_back();
  auto nstr = string_pool_.add(name);
  name_to_package_id_.emplace(nstr, pid);
  pnode.name_offset = nstr.offset;
  pnode.name_length = nstr.length;
  pnode.version_list_id = kVersionListEndId;
  return {pid, true};
}

std::pair<VersionId, bool> DiskGraph::create_version(PackageId pid, std::string_view version,
  ArchitectureType arch, DependencyId did_begin, DependencyCountType dcount) {
  const auto &pnode = package_nodes_[pid];
  for (auto vlid = pnode.version_list_id; vlid != kVersionListEndId;) {
    const auto &vlnode = version_lists_[vlid];
    for (auto vid = vlnode.version_id_begin; vid < vlnode.version_id_begin + vlnode.version_count; vid++) {
      const auto &vnode = version_nodes_[vid];
      auto vstr = string_pool_.get({vnode.version_offset, vnode.version_length});
      if (vstr == version && vnode.architecture == arch) return {vid, false};
    }
    vlid = vlnode.next_version_list_id;
  }
  VersionId vid = version_count();
  auto &vnode = version_nodes_.emplace_back();
  auto vstr = string_pool_.add(version);
  vnode.version_offset = vstr.offset;
  vnode.version_length = vstr.length;
  vnode.architecture = arch;
  vnode.dependency_count = dcount;
  vnode.dependency_id_begin = did_begin;
  return {vid, true};
}

std::pair<DependencyId, bool> DiskGraph::create_dependency(VersionId from_vid, PackageId to_pid,
  std::string_view version_constr, ArchitectureType arch_constr, DependencyType dep_type, GroupId group) {
  DependencyId did = dependency_count();
  auto &dedge = dependency_edges_.emplace_back();
  auto vcons_sv = string_pool_.add(version_constr);
  dedge.from_version_id = from_vid;
  dedge.to_package_id = to_pid;
  dedge.version_constraint_offset = vcons_sv.offset;
  dedge.version_constraint_length = vcons_sv.length;
  dedge.architecture_constraint = arch_constr;
  dedge.dependency_type = dep_type;
  dedge.group = group;
  return {did, true};
}

void DiskGraph::attach_versions(PackageId pid, VersionId vid_begin, VersionCountType vcount) {
  if (vcount == 0) return;
  auto &pnode = package_nodes_[pid];
  VersionListId vlid = version_lists_.size();
  auto &vlnode = version_lists_.emplace_back();
  vlnode.version_count = vcount;
  vlnode.version_id_begin = vid_begin;
  vlnode.next_version_list_id = pnode.version_list_id;
  pnode.version_list_id = vlid;
}

std::optional<PackageView> DiskGraph::get_package(std::string_view name) const {
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return std::nullopt;
  return {get_package(it->second)};
}

void DiskGraph::ingest(BufferGraph &bgraph) {
  for (const auto &bpnode : bgraph.package_nodes_) {
    VersionId vid_begin = version_count();
    VersionCountType vcount = 0;
    auto [pid, psucc] = create_package(bpnode.name);

    for (auto bvid : bpnode.version_ids) {
      const auto &bvnode = bgraph.version_nodes_[bvid];
      DependencyId did_begin = dependency_count();
      DependencyCountType dcount = bvnode.dependency_ids.size();
      auto [vid, vsucc] = create_version(pid, bvnode.version, bvnode.architecture, did_begin, dcount);
      if (!vsucc) continue;
      vcount++;

      for (auto bdid : bvnode.dependency_ids) {
        const auto &bdedge = bgraph.dependency_edges_[bdid];
        const auto &btpnode = bgraph.package_nodes_[bdedge.to_package_id];
        auto [tpid, tpsucc] = create_package(btpnode.name);
        create_dependency(
          vid, tpid, bdedge.version_constraint, bdedge.architecture_constraint, bdedge.dependency_type, bdedge.group);
      }
    }
    if (vcount > 0) attach_versions(pid, vid_begin, vcount);
  }
}

void DiskGraph::sync() {
  control().package_count = package_count();
  control().version_count = version_count();
  control().dependency_count = dependency_count();
  control().version_list_count = version_lists_.size();
  control().architecture_count = architectures_.size();
  control().dependency_type_count = dependency_types_.size();
  control().string_pool_size = string_pool_.size();

  control_file_.sync();
  package_nodes_.sync();
  version_nodes_.sync();
  dependency_edges_.sync();
  version_lists_.sync();
  architectures_.sync();
  dependency_types_.sync();
  string_pool_.sync();
}
