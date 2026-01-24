#pragma once
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>
#include "config.hpp"
#include "disk_vector.hpp"
#include "graph_view.hpp"
#include "string_map.hpp"
#include "string_pool.hpp"
#include "symbol_table.hpp"

class DiskGraph {
public:
  DiskGraph(std::size_t chunk_bytes = kDefaultChunkBytes) noexcept;
  DiskGraph(const std::filesystem::path &directory_path, open_mode mode = open_mode::kLoadOrCreate,
            std::initializer_list<std::string_view> architectures = {},
            std::initializer_list<std::string_view> dependency_types = {},
            std::size_t chunk_bytes = kDefaultChunkBytes) noexcept;
  ~DiskGraph() { close(); }

  open_code open(const std::filesystem::path &directory_path, open_mode mode = open_mode::kLoadOrCreate,
                 std::initializer_list<std::string_view> architectures = {},
                 std::initializer_list<std::string_view> dependency_types = {}) noexcept;
  void close();
  void sync();

  bool is_open() const noexcept { return control_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  std::size_t chunk_bytes() const noexcept { return package_nodes_.chunk_bytes(); }
  void set_chunk_bytes(std::size_t chunk_bytes) noexcept;

  std::size_t architecture_count() const noexcept { return architectures_.size(); }
  std::size_t dependency_type_count() const noexcept { return dependency_types_.size(); }

  std::size_t package_count() const noexcept { return package_nodes_.size(); }
  std::size_t version_count() const noexcept { return version_nodes_.size(); }
  std::size_t dependency_count() const noexcept { return dependency_edges_.size(); }

  const symbol_table<ArchitectureType> &architectures() const noexcept { return architectures_; }
  const symbol_table<DependencyType> &dependency_types() const noexcept { return dependency_types_; }

  PackageView get_package(PackageId pid) const noexcept;
  VersionView get_version(VersionId vid) const noexcept;
  DependencyView get_dependency(DependencyId did) const noexcept;

  std::optional<PackageView> get_package(std::string_view name) const noexcept;

  ArchitectureType add_architecture(std::string_view arch) noexcept;
  DependencyType add_dependency_type(std::string_view dtype) noexcept;

  void ingest(const BufferGraph &bgraph);

private:
  friend class DependencyGraph;
  friend class GpuGraph;
  struct PackageNode;
  struct VersionNode;
  struct DependencyEdge;
  struct VersionList;

  disk_vector<std::byte> control_;
  symbol_table<ArchitectureType> architectures_;
  symbol_table<DependencyType> dependency_types_;
  disk_vector<PackageNode> package_nodes_;
  disk_vector<VersionNode> version_nodes_;
  disk_vector<DependencyEdge> dependency_edges_;
  disk_vector<VersionList> version_lists_;
  string_pool<> string_pool_;
  string_handle_map<PackageId> name_to_package_id_;

  using VersionCountType = std::uint16_t;
  using DependencyCountType = std::uint16_t;
  using VersionListId = std::uint32_t;

  struct PackageNode {
    string_handle_offset_t name_offset;
    string_handle_length_t name_length;
    VersionListId version_list_id;
  };

  struct VersionNode {
    string_handle_offset_t version_offset;
    string_handle_length_t version_length;
    ArchitectureType architecture;
    DependencyCountType dependency_count;
    DependencyId dependency_id_begin;
  };

  struct DependencyEdge {
    VersionId from_version_id;
    PackageId to_package_id;
    string_handle_offset_t version_constraint_offset;
    string_handle_length_t version_constraint_length;
    ArchitectureType architecture_constraint;
    DependencyType dependency_type;
    GroupId group;
  };

  struct VersionList {
    VersionCountType version_count;
    VersionId version_id_begin;
    VersionListId next_version_list_id;
  };

  struct Control {
    std::size_t magic;
    std::size_t architecture_count;
    std::size_t dependency_type_count;
    std::size_t package_count;
    std::size_t version_count;
    std::size_t dependency_count;
    std::size_t version_list_count;
    std::size_t string_pool_size;
  };

  constexpr static VersionListId kVersionListEndId = static_cast<VersionListId>(-1);
  constexpr static std::size_t kMagicNumber = 0x485052474b534944; // "DISKGRPH"

  static std::size_t control_size() noexcept { return sizeof(Control); }

  Control &control() noexcept { return *reinterpret_cast<Control *>(control_.data()); }
  const Control &control() const noexcept { return *reinterpret_cast<const Control *>(control_.data()); }

  bool validate_control() const noexcept;

  bool load(const std::filesystem::path &directory_path) noexcept;
  bool create(const std::filesystem::path &directory_path, std::initializer_list<std::string_view> architectures,
              std::initializer_list<std::string_view> dependency_types) noexcept;

  std::pair<PackageId, bool> create_package(std::string_view name);
  std::pair<VersionId, bool> create_version(PackageId pid, std::string_view version, ArchitectureType arch,
                                            DependencyId did_begin, DependencyCountType dcount);
  std::pair<DependencyId, bool> create_dependency(VersionId from_vid, PackageId to_pid, std::string_view vcons,
                                                  ArchitectureType acons, DependencyType dtype, GroupId gid);

  void attach_versions(PackageId pid, VersionId vid_begin, VersionCountType vcount);
};
