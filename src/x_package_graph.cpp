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
  return storage_graph_.create(directory, kDefaultArchitectures, kDefaultDependencyTypes);
}

void XPackageGraph::open(const std::filesystem::path &directory, open_mode mode) {
  return storage_graph_.open(directory, mode, kDefaultArchitectures, kDefaultDependencyTypes);
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

std::optional<PackageView> XPackageGraph::get_package(std::string_view name) const
  noexcept { return storage_graph_.get_package(name); }

std::vector<std::string_view> XPackageGraph::query_packages(std::string_view architecture,
                                                            std::string_view prefix) const {
  return storage_graph_.query_packages(architecture, prefix);
}

nlohmann::ordered_json XPackageGraph::query_packages_json(std::string_view architecture, std::string_view prefix,
                                                          std::size_t limit, std::size_t offset) const {
  auto result = query_packages(architecture, prefix);
  auto begin = offset < result.size() ? result.begin() + offset : result.end();
  if (limit == 0) limit = -1ull;
  auto count = std::min(limit, static_cast<std::size_t>(result.end() - begin));
  auto shown = std::ranges::subrange(begin, begin + count);
  return {
    {"api_version", "v1"}, {"total", result.size()}, {"shown", shown.size()}, {"offset", offset},
    {"architecture_filtered", architecture}, {"prefix", prefix}, {"packages", shown}
  };
}

std::vector<VersionInfo> XPackageGraph::query_versions(std::string_view name, std::string_view architecture) const {
  return storage_graph_.query_versions(name, architecture);
}

nlohmann::ordered_json XPackageGraph::query_versions_json(std::string_view name, std::string_view architecture) const {
  return {
    {"api_version", "v1"}, {"package", name}, {"architecture_filtered", architecture},
    {"versions", query_versions(name, architecture)}
  };
}

std::variant<DependencyTree, DependencyFlat> XPackageGraph::query_dependencies(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool tree, bool use_gpu) const {
  if (depth == 1) tree = false;
  if (!use_gpu) return storage_graph_.query_dependencies(name, version, architecture, depth, tree);
  if (!cache_graph_.is_built()) cache_graph_.build();
  return cache_graph_.query_dependencies(name, version, architecture, depth, tree);
}

nlohmann::ordered_json XPackageGraph::query_dependencies_json(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree,
  bool use_gpu) const {
  auto result = query_dependencies(name, version, architecture, depth, tree, use_gpu);
  nlohmann::ordered_json json = {
    {"api_version", "v1"}, {"package", {{"name", name}, {"version", version}, {"architecture", architecture}}},
    {"max_depth", depth}, {"format", tree ? "tree" : "flat"}, {"use_gpu", use_gpu}
  };
  if (tree && depth > 1)
    json["dependencies"] = {
      {"single_dependencies", std::get<DependencyTree>(result).single_dependencies},
      {"alternative_dependencies", std::get<DependencyTree>(result).alternative_dependencies}
    };
  else json["dependencies"] = result;
  return json;
}

} // namespace xpg
