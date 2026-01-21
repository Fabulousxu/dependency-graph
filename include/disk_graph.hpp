#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include "config.hpp"
#include "disk_vector.hpp"
#include "graph_view.hpp"
#include "string_pool.hpp"
#include "symbol_table.hpp"

class BufferGraph;

class DiskGraph {
  friend class DependencyGraph;

  using VersionCountType = std::uint16_t;
  using DependencyCountType = std::uint16_t;
  using VersionListId = std::uint32_t;

  struct PackageNode {
    StringView::OffsetType name_offset;
    StringView::LengthType name_length;
    VersionListId version_list_id;
  };

  struct VersionNode {
    StringView::OffsetType version_offset;
    StringView::LengthType version_length;
    ArchitectureType architecture;
    DependencyCountType dependency_count;
    DependencyId dependency_id_begin;
  };

  struct DependencyEdge {
    VersionId from_version_id;
    PackageId to_package_id;
    StringView::OffsetType version_constraint_offset;
    StringView::LengthType version_constraint_length;
    ArchitectureType architecture_constraint;
    DependencyType dependency_type;
    GroupId group;
  };

  struct VersionList {
    VersionCountType version_count;
    VersionId version_id_begin;
    VersionListId next_version_list_id;
  };

public:
  DiskGraph(SymbolTable<ArchitectureType> &architectures, SymbolTable<DependencyType> &dependency_type,
    StringPool &string_pool, std::string_view directory_path, std::size_t chunk_bytes = kDefaultChunkBytes);

  ~DiskGraph() = default;

  std::size_t package_count() const noexcept { return package_nodes_.size(); }
  std::size_t version_count() const noexcept { return version_nodes_.size(); }
  std::size_t dependency_count() const noexcept { return dependency_edges_.size(); }

  PackageView get_package(PackageId pid) const;
  VersionView get_version(VersionId vid) const;
  DependencyView get_dependency(DependencyId did) const;

  std::optional<PackageView> get_package(std::string_view name) const;

  void ingest(BufferGraph &bgraph);

private:
  constexpr static VersionListId kVersionListEndId = static_cast<VersionListId>(-1);

  SymbolTable<ArchitectureType> &architectures_;
  SymbolTable<DependencyType> &dependency_types_;
  StringPool &string_pool_;
  DiskVector<PackageNode> package_nodes_;
  DiskVector<VersionNode> version_nodes_;
  DiskVector<DependencyEdge> dependency_edges_;
  DiskVector<VersionList> version_lists_;
  StringViewMap<PackageId> name_to_package_id_;

  std::pair<PackageId, bool> create_package_(std::string_view name);
  std::pair<VersionId, bool> create_version_(PackageId pid, std::string_view version, ArchitectureType arch,
    DependencyId did_begin, DependencyCountType dcount);
  std::pair<DependencyId, bool> create_dependency_(VersionId from_vid, PackageId to_pid,
    std::string_view version_constr, ArchitectureType arch_constr, DependencyType dep_type, GroupId group);

  void attach_versions_(PackageId pid, VersionId vid_begin, VersionCountType vcount);
};
