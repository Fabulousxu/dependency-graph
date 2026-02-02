#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>
#include "config.hpp"
#include "data_model.hpp"
#include "mmap_vector.hpp"
#include "string_pool.hpp"
#include "symbol_table.hpp"

class StorageGraph {
public:
  StorageGraph(std::size_t chunk_bytes = kDefaultChunkBytes) noexcept;
  StorageGraph(const std::filesystem::path &dir, open_mode mode = open_mode::kLoadOrCreate,
               std::initializer_list<std::string_view> archs = {}, std::initializer_list<std::string_view> dtypes = {},
               std::size_t chunk_bytes = kDefaultChunkBytes) noexcept;

  StorageGraph(const StorageGraph &) = delete;
  StorageGraph &operator=(const StorageGraph &) = delete;

  StorageGraph(StorageGraph &&) = default;
  StorageGraph &operator=(StorageGraph &&) = default;

  ~StorageGraph() = default;

  bool load(const std::filesystem::path &dir) noexcept;
  bool create(const std::filesystem::path &dir, std::initializer_list<std::string_view> archs,
              std::initializer_list<std::string_view> dtypes) noexcept;

  open_code open(const std::filesystem::path &dir, open_mode mode = open_mode::kLoadOrCreate,
                 std::initializer_list<std::string_view> archs = {},
                 std::initializer_list<std::string_view> dtypes = {}) noexcept;
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

  bool empty() const noexcept { return package_nodes_.empty(); }

  const auto &architectures() const noexcept { return architectures_; }
  const auto &dependency_types() const noexcept { return dependency_types_; }

  ArchitectureId intern_architecture(std::string_view arch) noexcept;
  DependencyTypeId intern_dependency_type(std::string_view dtype) noexcept;

  PackageView get_package(PackageId pid) const noexcept;
  std::optional<PackageView> get_package(std::string_view name) const noexcept;

  void ingest(const BufferGraph &bgraph);

  bool match_architecture(ArchitectureId origin, ArchitectureId target, ArchitectureId constr) const noexcept;

  std::vector<VersionId> init_frontier(std::string_view name, std::string_view version, std::string_view arch) const;
  DependencyResult query_dependencies(std::vector<VersionId> &frontier, std::size_t depth) const;

private:
  friend class CacheGraph;

  using StringOffset = std::uint32_t;
  using StringLength = std::uint8_t;
  using VersionCount = std::uint16_t;
  using DependencyCount = std::uint16_t;
  using VersionListId = std::uint32_t;

  struct PackageNode;
  struct VersionNode;
  struct DependencyEdge;
  struct VersionListNode;

  mmap_vector<std::byte> control_;
  symbol_table<ArchitectureId, StringLength> architectures_;
  symbol_table<DependencyTypeId, StringLength> dependency_types_;
  mmap_vector<PackageNode> package_nodes_;
  mmap_vector<VersionNode> version_nodes_;
  mmap_vector<DependencyEdge> dependency_edges_;
  mmap_vector<VersionListNode> version_list_nodes_;
  string_pool<StringLength> string_pool_;
  pooled_string_map<PackageId, StringLength> name_to_package_id_;

  struct PackageNode {
    StringOffset name;
    VersionListId version_list;
  };

  struct VersionNode {
    StringOffset version;
    ArchitectureId architecture;
    DependencyCount dependency_count;
    DependencyId dependency_begin;
  };

  struct DependencyEdge {
    VersionId from_version;
    PackageId to_package;
    StringOffset version_constraint;
    ArchitectureId architecture_constraint;
    DependencyTypeId dependency_type;
    GroupId group;
  };

  struct VersionListNode {
    VersionCount version_count;
    VersionId version_begin;
    VersionListId next;
  };

  struct ControlInfo {
    std::size_t magic;
    std::size_t architecture_count;
    std::size_t dependency_type_count;
    std::size_t package_count;
    std::size_t version_count;
    std::size_t dependency_count;
    std::size_t version_list_count;
    std::size_t string_pool_size;
  };

  constexpr static VersionListId version_list_end = static_cast<VersionListId>(-1);
  constexpr static std::size_t control_magic = 0x4850415247504544; // "DEPGRAPH"

  static constexpr std::size_t control_size() noexcept { return sizeof(ControlInfo); }

  ControlInfo &control() noexcept { return *reinterpret_cast<ControlInfo *>(control_.data()); }
  const ControlInfo &control() const noexcept { return *reinterpret_cast<const ControlInfo *>(control_.data()); }

  bool validate_control() const noexcept;

  VersionView get_version(VersionId vid) const noexcept;
  DependencyView get_dependency(DependencyId did) const noexcept;

  std::pair<PackageId, bool> create_package(std::string_view name);
  std::pair<VersionId, bool> create_version(PackageId pid, std::string_view version, ArchitectureId arch);
  std::pair<DependencyId, bool> create_dependency(VersionId from, PackageId to, std::string_view vcons,
                                                  ArchitectureId acons, DependencyTypeId dtype, GroupId group);

  void attach_versions(PackageId pid, VersionId vbegin, VersionCount vcount);
  void attach_dependencies(VersionId vid, DependencyId dbegin, DependencyCount dcount);

  DependencyInfo to_info(DependencyId did) const noexcept;

  DependencyLevel expand_frontier(std::vector<VersionId> &frontier, std::unordered_set<VersionId> &visited,
                                  bool has_next) const;
};
