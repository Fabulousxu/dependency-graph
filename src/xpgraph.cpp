#include <algorithm>
#include "xpgraph.hpp"

namespace xpg {

XPGraph::XPGraph(std::size_t memory_limit, std::size_t growth_bytes) noexcept
  : storage_graph_(growth_bytes), buffer_graph_(storage_graph_), cache_graph_(storage_graph_),
    memory_limit_(memory_limit) {}

XPGraph::XPGraph(const std::filesystem::path &directory, open_mode mode, std::size_t memory_limit,
                 std::size_t growth_bytes)
  : XPGraph(memory_limit, growth_bytes) { open(directory, mode); }

void XPGraph::create(const std::filesystem::path &directory) {
  storage_graph_.create(directory, kBasicArchitectures, kDependencyTypes);
}

void XPGraph::open(const std::filesystem::path &directory, open_mode mode) {
  storage_graph_.open(directory, mode, kBasicArchitectures, kDependencyTypes);
}

void XPGraph::close() {
  buffer_graph_.clear();
  storage_graph_.close();
  clear_cache();
}

bool XPGraph::delete_package(std::string_view name, std::string_view version, std::string_view architecture) {
  return storage_graph_.delete_package(name, version, architecture);
}

bool XPGraph::flush_buffer_if_needed(bool update_if_exists) {
  auto needed = estimated_memory_usage() >= memory_limit_;
  if (needed) flush_buffer(update_if_exists);
  return needed;
}

std::size_t XPGraph::estimated_memory_usage() const
  noexcept { return sizeof(XPGraph) + buffer_graph_.estimated_memory_usage() - sizeof(BufferGraph); }

ArchitectureId XPGraph::intern_architecture(std::string_view architecture) {
  return storage_graph_.intern_architecture(architecture);
}

DependencyType XPGraph::intern_dependency_type(std::string_view dependency_type) {
  return storage_graph_.intern_dependency_type(dependency_type);
}

std::optional<PackageView> XPGraph::get_package(std::string_view name) const
  noexcept { return storage_graph_.get_package(name); }

std::vector<std::string_view> XPGraph::query_packages(std::string_view architecture,
                                                      std::string_view prefix) const {
  return storage_graph_.query_packages(architecture, prefix);
}

std::vector<VersionInfo> XPGraph::query_versions(std::string_view name, std::string_view architecture) const {
  return storage_graph_.query_versions(name, architecture);
}

std::variant<DependencyTree, DependencyFlat> XPGraph::query_dependencies(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool tree, bool use_gpu, bool filter_architecture, bool filter_version, bool expand_alternative) const {
  if (depth == 1) tree = false;
  if (!use_gpu)
    return storage_graph_.query_dependencies(name, version, architecture, depth, tree, filter_architecture,
                                             filter_version, expand_alternative);
  if (!cache_graph_.is_built()) cache_graph_.build();
  return cache_graph_.query_dependencies(name, version, architecture, depth, tree, filter_architecture, filter_version,
                                         expand_alternative);
}

} // namespace xpg
