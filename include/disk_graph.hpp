#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>
#include "config.hpp"
#include "disk_vector.hpp"
#include "graph_view.hpp"
#include "string_map.hpp"
#include "string_pool.hpp"
#include "symbol_table.hpp"

class BufferGraph;

class DiskGraph {
  friend class DependencyGraph;

  using VersionCountType = std::uint16_t;
  using DependencyCountType = std::uint16_t;
  using VersionListId = std::uint32_t;

  struct PackageNode {
    StringHandleOffsetType name_offset;
    StringHandleLengthType name_length;
    VersionListId version_list_id;
  };

  struct VersionNode {
    StringHandleOffsetType version_offset;
    StringHandleLengthType version_length;
    ArchitectureType architecture;
    DependencyCountType dependency_count;
    DependencyId dependency_id_begin;
  };

  struct DependencyEdge {
    VersionId from_version_id;
    PackageId to_package_id;
    StringHandleOffsetType version_constraint_offset;
    StringHandleLengthType version_constraint_length;
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
  DiskGraph(const std::filesystem::path &directory_path, OpenMode mode = kLoadOrCreate,
    std::initializer_list<std::string_view> architectures = {},
    std::initializer_list<std::string_view> dependency_types = {}, std::size_t chunk_bytes = kDefaultChunkBytes);
  ~DiskGraph() { sync(); }

  std::size_t package_count() const noexcept { return package_nodes_.size(); }
  std::size_t version_count() const noexcept { return version_nodes_.size(); }
  std::size_t dependency_count() const noexcept { return dependency_edges_.size(); }

  PackageView get_package(PackageId pid) const;
  VersionView get_version(VersionId vid) const;
  DependencyView get_dependency(DependencyId did) const;

  std::optional<PackageView> get_package(std::string_view name) const;

  void ingest(BufferGraph &bgraph);

  void sync();

private:
  DiskVector<std::byte> control_file_;
  DiskVector<PackageNode> package_nodes_;
  DiskVector<VersionNode> version_nodes_;
  DiskVector<DependencyEdge> dependency_edges_;
  DiskVector<VersionList> version_lists_;
  SymbolTable<ArchitectureType> architectures_;
  SymbolTable<DependencyType> dependency_types_;
  StringPool<false> string_pool_;
  StringHandleMap<PackageId> name_to_package_id_;

  struct Control {
    std::size_t magic;
    std::size_t package_count;
    std::size_t version_count;
    std::size_t dependency_count;
    std::size_t version_list_count;
    std::size_t architecture_count;
    std::size_t dependency_type_count;
    std::size_t string_pool_size;
  };

  constexpr static VersionListId kVersionListEndId = static_cast<VersionListId>(-1);
  constexpr static std::size_t kMagicNumber = 0x485052474b534944; // "DISKGRPH"

  Control &control() noexcept { return *reinterpret_cast<Control *>(control_file_.data()); }
  const Control &control() const noexcept { return *reinterpret_cast<const Control *>(control_file_.data()); }
  static std::size_t control_size() noexcept { return sizeof(Control); }
  bool validate_control() const noexcept;

  std::pair<PackageId, bool> create_package(std::string_view name);
  std::pair<VersionId, bool> create_version(PackageId pid, std::string_view version, ArchitectureType arch,
    DependencyId did_begin, DependencyCountType dcount);
  std::pair<DependencyId, bool> create_dependency(VersionId from_vid, PackageId to_pid,
    std::string_view version_constr, ArchitectureType arch_constr, DependencyType dep_type, GroupId group);

  void attach_versions(PackageId pid, VersionId vid_begin, VersionCountType vcount);
};
