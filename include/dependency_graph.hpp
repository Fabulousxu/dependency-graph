#pragma once
#include <optional>
#include <string_view>
#include <utility>
#include "buffer_graph.hpp"
#include "disk_graph.hpp"
#include "gpu_graph.hpp"
#include "result_model.hpp"
#include "string_pool.hpp"
#include "symbol_table.hpp"

constexpr std::size_t kDefaultMemoryThreshold = 1024 * 1024 * 1024;

class DependencyGraph {
public:
  DependencyGraph(std::string_view path, std::size_t memory_thresh = kDefaultMemoryThreshold,
    std::size_t chunk_bytes = kDefaultChunkBytes);
  ~DependencyGraph() { flush_to_disk(); }

  SymbolTable<ArchitectureType> &architectures() { return architectures_; }
  const SymbolTable<ArchitectureType> &architectures() const { return architectures_; }
  SymbolTable<DependencyType> &dependency_types() { return dependency_types_; }
  const SymbolTable<DependencyType> &dependency_types() const { return dependency_types_; }

  std::size_t package_count() const noexcept { return disk_graph_.package_count(); }
  std::size_t version_count() const noexcept { return disk_graph_.version_count(); }
  std::size_t dependency_count() const noexcept { return disk_graph_.dependency_count(); }
  std::size_t buffer_package_count() const noexcept { return buf_graph_.package_count(); }
  std::size_t buffer_version_count() const noexcept { return buf_graph_.version_count(); }
  std::size_t buffer_dependency_count() const noexcept { return buf_graph_.dependency_count(); }

  PackageView get_package(PackageId pid) const { return disk_graph_.get_package(pid); }
  VersionView get_version(VersionId vid) const { return disk_graph_.get_version(vid); }
  DependencyView get_dependency(DependencyId did) const { return disk_graph_.get_dependency(did); }

  std::optional<PackageView> get_package(std::string_view name) const { return disk_graph_.get_package(name); }

  std::pair<PackageId, bool> create_package(std::string_view name);
  std::pair<VersionId, bool> create_version(PackageId pid, std::string_view version, ArchitectureType arch);
  std::pair<DependencyId, bool> create_dependency(VersionId from_vid, PackageId to_pid, std::string_view version_constr,
    ArchitectureType arch_constr, DependencyType dep_type, GroupId group);

  void sync_to_gpu() { gpu_graph_.build(*this); }
  void flush_to_disk();
  bool flush_to_disk_if_needed();
  std::size_t memory_threshold() const noexcept { return memory_threshold_; }
  void set_memory_threshold(std::size_t memory_thresh) noexcept { memory_threshold_ = memory_thresh; }
  std::size_t estimated_memory_usage() const noexcept;

  DependencyResult query_dependencies(std::string_view name, std::string_view version, std::string_view arch,
    std::size_t depth, bool use_gpu) const;

  DependencyResult query_dependencies_on_buffer(std::string_view name, std::string_view version, std::string_view arch,
    std::size_t depth) const;

private:
  SymbolTable<ArchitectureType> architectures_;
  SymbolTable<DependencyType> dependency_types_;
  StringPool string_pool_;
  DiskGraph disk_graph_;
  BufferGraph buf_graph_;
  GpuGraph gpu_graph_;
  std::size_t memory_threshold_;

  DependencyResult query_dependencies_on_disk_(std::vector<VersionId> &frontier, std::size_t depth) const;
  DependencyResult query_dependencies_on_gpu_(std::vector<VersionId> &frontier, std::size_t depth) const;
};
