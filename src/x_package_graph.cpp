#include <algorithm>
#include "x_package_graph.hpp"

namespace xpg {

XPackageGraph::XPackageGraph(std::size_t memory_limit, std::size_t growth_bytes) noexcept
  : storage_graph_(growth_bytes), buffer_graph_(storage_graph_), cache_graph_(storage_graph_),
    memory_limit_(memory_limit) {}

XPackageGraph::XPackageGraph(const std::filesystem::path &directory, open_mode mode, std::size_t memory_limit,
                             std::size_t growth_bytes)
  : XPackageGraph(memory_limit, growth_bytes) { open(directory, mode); }

void XPackageGraph::create(const std::filesystem::path &directory) {
  storage_graph_.create(directory, kDefaultArchitectures, kDefaultDependencyTypes);
}

void XPackageGraph::open(const std::filesystem::path &directory, open_mode mode) {
  storage_graph_.open(directory, mode, kDefaultArchitectures, kDefaultDependencyTypes);
}

void XPackageGraph::close() {
  buffer_graph_.clear();
  storage_graph_.close();
  clear_cache();
}

bool XPackageGraph::delete_package(std::string_view name, std::string_view version, std::string_view architecture) {
  return storage_graph_.delete_package(name, version, architecture);
}

bool XPackageGraph::flush_buffer_if_needed(bool update_if_exists) {
  auto needed = estimated_memory_usage() >= memory_limit_;
  if (needed) flush_buffer(update_if_exists);
  return needed;
}

std::size_t XPackageGraph::estimated_memory_usage() const
  noexcept { return sizeof(XPackageGraph) + buffer_graph_.estimated_memory_usage() - sizeof(BufferGraph); }

ArchitectureId XPackageGraph::intern_architecture(std::string_view architecture) {
  return storage_graph_.intern_architecture(architecture);
}

DependencyType XPackageGraph::intern_dependency_type(std::string_view dependency_type) {
  return storage_graph_.intern_dependency_type(dependency_type);
}

std::optional<PackageView> XPackageGraph::get_package(std::string_view name) const
  noexcept { return storage_graph_.get_package(name); }

std::vector<std::string_view> XPackageGraph::query_packages(std::string_view architecture,
                                                            std::string_view prefix) const {
  return storage_graph_.query_packages(architecture, prefix);
}

std::vector<VersionInfo> XPackageGraph::query_versions(std::string_view name, std::string_view architecture) const {
  return storage_graph_.query_versions(name, architecture);
}

std::variant<DependencyTree, DependencyFlat> XPackageGraph::query_dependencies(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool tree, bool use_gpu) const {
  if (depth == 1) tree = false;
  if (!use_gpu) return storage_graph_.query_dependencies(name, version, architecture, depth, tree);
  if (!cache_graph_.is_built()) cache_graph_.build();
  return cache_graph_.query_dependencies(name, version, architecture, depth, tree);
}

} // namespace xpg
