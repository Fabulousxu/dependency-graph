#pragma once
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>
#include "buffer_graph.hpp"
#include "config.hpp"
#include "disk_graph.hpp"
#include "gpu_graph.hpp"
#include "graph_view.hpp"
#include "result_model.hpp"
#include "symbol_table.hpp"

class DependencyGraph {
public:
  DependencyGraph(std::size_t memory_limit = kDefaultMemoryLimit,
                  std::size_t chunk_bytes = kDefaultChunkBytes) noexcept;
  DependencyGraph(const std::filesystem::path &directory_path, open_mode mode = open_mode::kLoadOrCreate,
                  std::size_t memory_limit = kDefaultMemoryLimit,
                  std::size_t chunk_bytes = kDefaultChunkBytes) noexcept;
  ~DependencyGraph() { close(); }

  open_code open(const std::filesystem::path &directory_path, open_mode mode = open_mode::kLoadOrCreate) noexcept;
  void close();
  void sync() { disk_graph_.sync(); }

  void flush_buffer();
  bool flush_buffer_if_needed();

  void sync_gpu() { gpu_graph_.build(disk_graph_, kDefaultMaxDeviceVectorBytes); }
  void free_gpu() { gpu_graph_.free(); }

  std::size_t memory_limit() const noexcept { return memory_limit_; }
  void set_memory_limit(std::size_t memory_limit) noexcept { memory_limit_ = memory_limit; }

  std::size_t estimated_memory_usage() const noexcept;

  std::size_t architecture_count() const noexcept { return disk_graph_.architecture_count(); }
  std::size_t dependency_type_count() const noexcept { return disk_graph_.dependency_type_count(); }

  std::size_t package_count() const noexcept { return disk_graph_.package_count(); }
  std::size_t version_count() const noexcept { return disk_graph_.version_count(); }
  std::size_t dependency_count() const noexcept { return disk_graph_.dependency_count(); }

  std::size_t buffer_package_count() const noexcept { return buf_graph_.package_count(); }
  std::size_t buffer_version_count() const noexcept { return buf_graph_.version_count(); }
  std::size_t buffer_dependency_count() const noexcept { return buf_graph_.dependency_count(); }

  const symbol_table<ArchitectureType> &architectures() const noexcept { return disk_graph_.architectures_; }
  const symbol_table<DependencyType> &dependency_types() const noexcept { return disk_graph_.dependency_types_; }

  PackageView get_package(PackageId pid) const noexcept { return disk_graph_.get_package(pid); }
  VersionView get_version(VersionId vid) const noexcept { return disk_graph_.get_version(vid); }
  DependencyView get_dependency(DependencyId did) const noexcept { return disk_graph_.get_dependency(did); }

  std::optional<PackageView> get_package(std::string_view name) const noexcept { return disk_graph_.get_package(name); }

  ArchitectureType add_architecture(std::string_view version) { return disk_graph_.add_architecture(version); }
  DependencyType add_dependency_type(std::string_view dtype) { return disk_graph_.add_dependency_type(dtype); }

  std::pair<PackageId, bool> create_package(std::string_view name);
  std::pair<VersionId, bool> create_version(PackageId pid, std::string_view version, ArchitectureType arch);
  std::pair<DependencyId, bool> create_dependency(VersionId from_vid, PackageId to_pid, std::string_view vcons,
                                                  ArchitectureType acons, DependencyType dtype, GroupId gid);

  DependencyResult query_dependencies(std::string_view name, std::string_view version, std::string_view arch,
                                      std::size_t depth, bool use_gpu) const;
  DependencyResult query_dependencies_on_buffer(std::string_view name, std::string_view version, std::string_view arch,
                                                std::size_t depth) const;

private:
  friend class GpuGraph;

  DiskGraph disk_graph_;
  BufferGraph buf_graph_;
  GpuGraph gpu_graph_;
  std::size_t memory_limit_;

  DependencyResult query_dependencies_on_disk(std::vector<VersionId> &frontier, std::size_t depth) const;
  DependencyResult query_dependencies_on_gpu(std::vector<VersionId> &frontier, std::size_t depth) const;
};
