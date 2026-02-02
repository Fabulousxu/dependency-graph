#include "dependency_graph.hpp"

DependencyGraph::DependencyGraph(std::size_t memory_limit, std::size_t chunk_bytes) noexcept
  : storage_graph_(chunk_bytes), buffer_graph_(storage_graph_), cache_graph_(storage_graph_),
    memory_limit_(memory_limit) {}

DependencyGraph::DependencyGraph(const std::filesystem::path &dir, open_mode mode, std::size_t memory_limit,
                                 std::size_t chunk_bytes) noexcept
  : DependencyGraph(memory_limit, chunk_bytes) {
  open(dir, mode);
}

bool DependencyGraph::create(const std::filesystem::path &dir) noexcept {
  return storage_graph_.create(dir, default_architectures, default_dependency_types);
}

open_code DependencyGraph::open(const std::filesystem::path &dir, open_mode mode) noexcept {
  return storage_graph_.open(dir, mode, default_architectures, default_dependency_types);
}

void DependencyGraph::close() {
  flush_buffer();
  storage_graph_.close();
  free_gpu();
}

void DependencyGraph::flush_buffer() {
  storage_graph_.ingest(buffer_graph_);
  buffer_graph_.clear();
}

bool DependencyGraph::flush_buffer_if_needed() {
  auto needed = estimated_memory_usage() >= memory_limit_;
  if (needed) flush_buffer();
  return needed;
}

std::size_t DependencyGraph::estimated_memory_usage() const noexcept {
  return sizeof(DependencyGraph) + buffer_graph_.estimated_memory_usage() - sizeof(BufferGraph);
}

std::optional<PackageView> DependencyGraph::get_package(std::string_view name) const noexcept {
  return storage_graph_.get_package(name);
}

DependencyResult DependencyGraph::query_dependencies(std::string_view name, std::string_view version,
                                                     std::string_view arch, std::size_t depth, bool use_gpu) const {
  auto frontier = storage_graph_.init_frontier(name, version, arch);
  if (use_gpu) return cache_graph_.query_dependencies(frontier, depth);
  return storage_graph_.query_dependencies(frontier, depth);
}

DependencyResult DependencyGraph::query_dependencies_on_buffer(std::string_view name, std::string_view version,
                                                               std::string_view arch, std::size_t depth) const {
  auto frontier = buffer_graph_.init_frontier(name, version, arch);
  return buffer_graph_.query_dependencies(frontier, depth);
}
