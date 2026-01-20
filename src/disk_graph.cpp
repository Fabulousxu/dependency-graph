#include "disk_graph.hpp"
#include "buffer_graph.hpp"

DiskGraph::DiskGraph(SymbolTable<ArchitectureType> &architectures, SymbolTable<DependencyType> &dependency_type,
  StringPool &string_pool, std::string_view directory_path, std::size_t chunk_bytes)
  : architectures_{architectures}, dependency_types_{dependency_type}, string_pool_{string_pool},
    package_nodes_{std::string{directory_path} + "/packages.dat", chunk_bytes},
    version_nodes_{std::string{directory_path} + "/versions.dat", chunk_bytes},
    dependency_edges_{std::string{directory_path} + "/dependencies.dat", chunk_bytes},
    version_lists_{std::string{directory_path} + "/version_lists.dat", chunk_bytes},
    name_to_package_id_{0, {string_pool_}, {string_pool_}} {}

PackageView DiskGraph::get_package(PackageId pid) const {
  PackageView pview;
  const auto &pnode = package_nodes_[pid];
  pview.id = pid;
  pview.name = string_pool_.get({pnode.name_offset, pnode.name_length});
  pview.versions = [this, pid] {
    std::vector<VersionView> vviews;
    const auto &pnode = package_nodes_[pid];
    for (auto vid = pnode.version_id_begin; vid < pnode.version_id_begin + pnode.version_count; vid++)
      vviews.emplace_back(get_version(vid));
    for (auto vlid = pnode.next_version_list_id; vlid != kVersionListEndId;) {
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

std::pair<PackageId, bool> DiskGraph::create_package_(std::string_view name) {
  if (auto it = name_to_package_id_.find(name); it != name_to_package_id_.end()) return {it->second, false};
  PackageId pid = package_nodes_.size();
  auto &pnode = package_nodes_.emplace_back();
  auto nstr = string_pool_.add(name);
  name_to_package_id_.emplace(nstr, pid);
  pnode.name_offset = nstr.offset;
  pnode.name_length = nstr.length;
  pnode.version_count = 0;
  pnode.version_id_begin = 0;
  pnode.next_version_list_id = kVersionListEndId;
  return {pid, true};
}


std::pair<VersionId, bool> DiskGraph::create_version_(PackageId pid, std::string_view version,
  ArchitectureType arch, DependencyId did_begin, DependencyCountType dcount) {
  const auto &pnode = package_nodes_[pid];
  for (auto vid = pnode.version_id_begin; vid < pnode.version_id_begin + pnode.version_count; vid++) {
    const auto &vnode = version_nodes_[vid];
    auto vstr = string_pool_.get({vnode.version_offset, vnode.version_length});
    if (vstr == version && vnode.architecture == arch) return {vid, false};
  }
  for (auto vlid = pnode.next_version_list_id; vlid != kVersionListEndId;) {
    const auto &vlnode = version_lists_[vlid];
    for (auto vid = vlnode.version_id_begin; vid < vlnode.version_id_begin + vlnode.version_count; vid++) {
      const auto &vnode = version_nodes_[vid];
      auto vstr = string_pool_.get({vnode.version_offset, vnode.version_length});
      if (vstr == version && vnode.architecture == arch) return {vid, false};
    }
    vlid = vlnode.next_version_list_id;
  }
  VersionId vid = version_nodes_.size();
  auto &vnode = version_nodes_.emplace_back();
  auto vstr = string_pool_.add(version);
  vnode.version_offset = vstr.offset;
  vnode.version_length = vstr.length;
  vnode.architecture = arch;
  vnode.dependency_count = dcount;
  vnode.dependency_id_begin = did_begin;
  return {vid, true};
}

std::pair<DependencyId, bool> DiskGraph::create_dependency_(VersionId from_vid, PackageId to_pid,
  std::string_view version_constr, ArchitectureType arch_constr, DependencyType dep_type, GroupId group) {
  DependencyId did = dependency_edges_.size();
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

void DiskGraph::attach_versions_(PackageId pid, VersionId vid_begin, VersionCountType vcount) {
  if (vcount == 0) return;
  auto &pnode = package_nodes_[pid];
  if (pnode.version_count == 0) {
    pnode.version_count = vcount;
    pnode.version_id_begin = vid_begin;
    return;
  }
  VersionListId vlid = version_lists_.size();
  auto &vlnode = version_lists_.emplace_back();
  vlnode.version_count = vcount;
  vlnode.version_id_begin = vid_begin;
  vlnode.next_version_list_id = pnode.next_version_list_id;
  pnode.next_version_list_id = vlid;
}

std::optional<PackageView> DiskGraph::get_package(std::string_view name) const {
  auto it = name_to_package_id_.find(name);
  if (it == name_to_package_id_.end()) return std::nullopt;
  return {get_package(it->second)};
}

void DiskGraph::ingest(BufferGraph &graphbuf) {
  for (const auto &mpnode : graphbuf.package_nodes_) {
    VersionCountType vcount = 0;
    VersionId vid_begin = version_count();
    auto [pid, psucc] = create_package_(mpnode.name);

    for (auto mvid : mpnode.version_ids) {
      const auto &mvnode = graphbuf.version_nodes_[mvid];
      DependencyCountType dcount = mvnode.dependency_ids.size();
      DependencyId did_begin = dependency_count();
      auto [vid, vsucc] = create_version_(pid, mvnode.version, mvnode.architecture, did_begin, dcount);
      if (!vsucc) continue;
      vcount++;

      for (auto mdid : mvnode.dependency_ids) {
        const auto &mdedge = graphbuf.dependency_edges_[mdid];
        const auto &mdpnode = graphbuf.package_nodes_[mdedge.to_package_id];
        auto [tpid, tpsucc] = create_package_(mdpnode.name);
        create_dependency_(
          vid, tpid, mdedge.version_constraint, mdedge.architecture_constraint, mdedge.dependency_type, mdedge.group);
      }
    }
    if (vcount > 0) attach_versions_(pid, vid_begin, vcount);
  }
}
