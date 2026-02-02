#pragma once
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>
#include "buffer_graph.hpp"
#include "cache_graph.hpp"
#include "config.hpp"
#include "data_model.hpp"
#include "storage_graph.hpp"
#include "symbol_table.hpp"

class DependencyGraph {
public:
  DependencyGraph(std::size_t memory_limit = kDefaultMemoryLimit,
                  std::size_t chunk_bytes = kDefaultChunkBytes) noexcept;
  DependencyGraph(const std::filesystem::path &dir, open_mode mode = open_mode::kLoadOrCreate,
                  std::size_t memory_limit = kDefaultMemoryLimit,
                  std::size_t chunk_bytes = kDefaultChunkBytes) noexcept;

  DependencyGraph(const DependencyGraph &) = delete;
  DependencyGraph &operator=(const DependencyGraph &) = delete;

  DependencyGraph(DependencyGraph &&) = default;
  DependencyGraph &operator=(DependencyGraph &&) = delete;

  ~DependencyGraph() { close(); }

  bool load(const std::filesystem::path &dir) noexcept { return storage_graph_.load(dir); }
  bool create(const std::filesystem::path &dir) noexcept;

  open_code open(const std::filesystem::path &dir, open_mode mode = open_mode::kLoadOrCreate) noexcept;
  void close();
  void sync() { storage_graph_.sync(); }

  void flush_buffer();
  bool flush_buffer_if_needed();

  void build_cache() { cache_graph_.build_cache(); }
  void free_gpu() { cache_graph_.free_gpu(); }

  std::size_t chunk_bytes() const noexcept { return storage_graph_.chunk_bytes(); }
  void set_chunk_bytes(std::size_t chunk_bytes) noexcept { storage_graph_.set_chunk_bytes(chunk_bytes); }

  std::size_t memory_limit() const noexcept { return memory_limit_; }
  void set_memory_limit(std::size_t memory_limit) noexcept { memory_limit_ = memory_limit; }

  std::size_t estimated_memory_usage() const noexcept;

  std::size_t architecture_count() const noexcept { return storage_graph_.architecture_count(); }
  std::size_t dependency_type_count() const noexcept { return storage_graph_.dependency_type_count(); }

  std::size_t package_count() const noexcept { return storage_graph_.package_count(); }
  std::size_t version_count() const noexcept { return storage_graph_.version_count(); }
  std::size_t dependency_count() const noexcept { return storage_graph_.dependency_count(); }

  std::size_t buffer_package_count() const noexcept { return buffer_graph_.package_count(); }
  std::size_t buffer_version_count() const noexcept { return buffer_graph_.version_count(); }
  std::size_t buffer_dependency_count() const noexcept { return buffer_graph_.dependency_count(); }

  const auto &architectures() const noexcept { return storage_graph_.architectures(); }
  const auto &dependency_types() const noexcept { return storage_graph_.dependency_types(); }

  PackageView get_package(PackageId pid) const noexcept { return storage_graph_.get_package(pid); }
  std::optional<PackageView> get_package(std::string_view name) const noexcept;

  void create_package(const PackageInfo &info) { buffer_graph_.create_package(info); }

  DependencyResult query_dependencies(std::string_view name, std::string_view version, std::string_view arch,
                                      std::size_t depth, bool use_gpu) const;

  DependencyResult query_dependencies_on_buffer(std::string_view name, std::string_view version, std::string_view arch,
                                                std::size_t depth) const;

private:
  StorageGraph storage_graph_;
  BufferGraph buffer_graph_;
  CacheGraph cache_graph_;
  std::size_t memory_limit_;

  static constexpr std::initializer_list<std::string_view> default_architectures = {"native", "any", "all"};
  static constexpr std::initializer_list<std::string_view> default_dependency_types = {
    "Depends", "Pre-Depends", "Recommends", "Suggests", "Breaks", "Conflicts", "Provides", "Replaces", "Enhances"
  };
};
