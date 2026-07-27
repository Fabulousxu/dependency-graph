#include <algorithm>
#include <filesystem>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include "xpgraph.hpp"

namespace py = pybind11;

xpg::open_mode parse_open_mode(std::string_view mode) {
  if (mode == "load") return xpg::open_mode::kLoad;
  if (mode == "create") return xpg::open_mode::kCreate;
  if (mode == "load-or-create") return xpg::open_mode::kLoadOrCreate;
  throw std::invalid_argument(std::format("Invalid open_mode: {}", mode));
}

template <>
class py::detail::type_caster<xpg::VersionInfo> {
public:
  PYBIND11_TYPE_CASTER(xpg::VersionInfo, _("VersionInfo"));
  static handle cast(const xpg::VersionInfo &info, return_value_policy, handle) {
    dict d;
    d["version"] = info.version;
    d["architecture"] = info.architecture;
    return d.release();
  }
};

template <>
class py::detail::type_caster<xpg::DependencyTree> {
public:
  PYBIND11_TYPE_CASTER(xpg::DependencyTree, _("DependencyTree"));
  static handle cast(const xpg::DependencyTree &tree, return_value_policy, handle) {
    dict d, dependencies;
    d["name"] = tree.name;
    d["type"] = tree.type;
    d["version_constraint"] = tree.version_constraint;
    d["architecture"] = tree.architecture_constraint;
    dependencies["single_dependencies"] = tree.single_dependencies;
    dependencies["alternative_dependencies"] = tree.alternative_dependencies;
    d["dependencies"] = std::move(dependencies);
    return d.release();
  }
};

template <>
class py::detail::type_caster<xpg::DependencyInfo> {
public:
  PYBIND11_TYPE_CASTER(xpg::DependencyInfo, _("DependencyInfo"));
  static handle cast(const xpg::DependencyInfo &info, return_value_policy, handle) {
    dict d;
    d["name"] = info.name;
    d["type"] = info.type;
    d["version_constraint"] = info.version_constraint;
    d["architecture"] = info.architecture_constraint;
    return d.release();
  }
};

template <>
class py::detail::type_caster<xpg::DependencyFlat> {
public:
  PYBIND11_TYPE_CASTER(xpg::DependencyFlat, _("DependencyFlat"));
  static handle cast(const xpg::DependencyFlat &flat, return_value_policy, handle) {
    dict d;
    for (auto level : std::views::iota(0ull, flat.size())) {
      dict dlevel;
      dlevel["single_dependencies"] = flat[level].single_dependencies;
      dlevel["alternative_dependencies"] = flat[level].alternative_dependencies;
      d[str("depth " + std::to_string(level + 1))] = std::move(dlevel);
    }
    return d.release();
  }
};

class PyXPGraph {
public:
  PyXPGraph(const std::filesystem::path &data_directory, std::string_view open_mode = "load-or-create",
            std::size_t memory_limit = -1ull, std::size_t growth_bytes = xpg::kGrowthBytes)
    : graph_(data_directory, parse_open_mode(open_mode), xpg::kArchitectureTypes, xpg::kDependencyTypes,
             memory_limit, growth_bytes) {}

  void load(const std::filesystem::path &directory) { graph_.load(directory); }
  void create(const std::filesystem::path &directory) { graph_.create(directory); }
  void open(const std::filesystem::path &directory, std::string_view open_mode = "load-or-create") {
    graph_.open(directory, parse_open_mode(open_mode));
  }
  void sync() { graph_.sync(); }
  void close() { graph_.close(); }
  bool is_open() const noexcept { return graph_.is_open(); }

  std::size_t growth_bytes() const noexcept { return graph_.growth_bytes(); }
  void set_growth_bytes(std::size_t growth_bytes) noexcept { graph_.set_growth_bytes(growth_bytes); }
  std::size_t memory_limit() const noexcept { return graph_.flush_limit_bytes(); }
  void set_memory_limit(std::size_t memory_limit) noexcept { graph_.set_flush_limit_bytes(memory_limit); }
  std::size_t package_count() const noexcept { return graph_.package_count(); }
  std::size_t version_count() const noexcept { return graph_.version_count(); }
  std::size_t dependency_count() const noexcept { return graph_.dependency_count(); }

  std::vector<std::string_view> architectures() const noexcept {
    std::vector<std::string_view> result;
    for (auto arch : graph_.architecture_types()) result.emplace_back(arch);
    return result;
  }

  std::vector<std::string_view> dependency_types() const noexcept {
    std::vector<std::string_view> result;
    for (auto type : graph_.dependency_types()) result.emplace_back(type);
    return result;
  }

  std::size_t estimated_memory_usage() const noexcept { return graph_.estimated_memory_usage(); }
  void flush_buffer(bool update_if_exists = false) { graph_.flush_buffer(update_if_exists); }
  bool flush_buffer_if_needed(bool update_if_exists = false) { return graph_.flush_buffer_if_needed(update_if_exists); }
  void build_cache() const { graph_.build_cache(); }
  void clear_cache() const { graph_.clear_cache(); }
  void compact() { graph_.compact(); }

  std::tuple<std::size_t, std::vector<std::string_view>> query_packages(
    std::size_t limit = 0, std::size_t offset = 0, std::string_view architecture = "",
    std::string_view prefix = "") const {
    auto result = graph_.query_packages(architecture, prefix);
    auto begin = offset < result.size() ? result.begin() + offset : result.end();
    if (limit == 0) limit = -1ull;
    auto count = std::min(limit, static_cast<std::size_t>(result.end() - begin));
    return {result.size(), std::vector(begin, begin + count)};
  }

  std::vector<xpg::VersionInfo> query_versions(std::string_view name, std::string_view architecture = "") const {
    return graph_.query_versions(name, architecture);
  }

  py::dict query_dependencies(std::string_view name, std::string_view version = "", std::string_view architecture = "",
                              std::size_t depth = 1, bool tree = true, bool use_gpu = false,
                              bool satisfy_architecture = true, bool satisfy_version = true,
                              bool expand_alternative = true) const {
    auto result = graph_.query_dependencies(name, version, architecture, depth, tree, use_gpu, satisfy_architecture,
                                            satisfy_version, expand_alternative);
    if (!tree || depth == 1) return py::cast(result);
    py::dict d;
    d["single_dependencies"] = std::get<xpg::DependencyTree>(result).single_dependencies;
    d["alternative_dependencies"] = std::get<xpg::DependencyTree>(result).alternative_dependencies;
    return d;
  }

private:
  xpg::XPGraph graph_;
};

PYBIND11_MODULE(pyxpgraph, m) {
  m.doc() = "Python binding for XPGraph query APIs";

  py::class_<PyXPGraph>(m, "XPGraph")
    .def(py::init<const std::filesystem::path &, std::string_view, std::size_t, std::size_t>(),
         py::arg("data_directory"), py::arg("open_mode") = "load-or-create", py::arg("memory_limit") = -1ull,
         py::arg("growth_bytes") = xpg::kGrowthBytes)
    .def("load", &PyXPGraph::load, py::arg("directory"))
    .def("create", &PyXPGraph::create, py::arg("directory"))
    .def("open", &PyXPGraph::open, py::arg("directory"), py::arg("open_mode") = "load-or-create")
    .def("sync", &PyXPGraph::sync)
    .def("close", &PyXPGraph::close)
    .def("is_open", &PyXPGraph::is_open)
    .def("growth_bytes", &PyXPGraph::growth_bytes)
    .def("set_growth_bytes", &PyXPGraph::set_growth_bytes, py::arg("growth_bytes"))
    .def("memory_limit", &PyXPGraph::memory_limit)
    .def("set_memory_limit", &PyXPGraph::set_memory_limit, py::arg("memory_limit"))
    .def("package_count", &PyXPGraph::package_count)
    .def("version_count", &PyXPGraph::version_count)
    .def("dependency_count", &PyXPGraph::dependency_count)
    .def("architectures", &PyXPGraph::architectures)
    .def("dependency_types", &PyXPGraph::dependency_types)
    .def("estimated_memory_usage", &PyXPGraph::estimated_memory_usage)
    .def("flush_buffer", &PyXPGraph::flush_buffer, py::arg("update_if_exists") = false)
    .def("flush_buffer_if_needed", &PyXPGraph::flush_buffer_if_needed, py::arg("update_if_exists") = false)
    .def("build_cache", &PyXPGraph::build_cache)
    .def("clear_cache", &PyXPGraph::clear_cache)
    .def("compact", &PyXPGraph::compact)
    .def("query_packages", &PyXPGraph::query_packages, py::arg("limit") = 0, py::arg("offset") = 0,
         py::arg("architecture") = "", py::arg("prefix") = "")
    .def("query_versions", &PyXPGraph::query_versions, py::arg("name"), py::arg("architecture") = "")
    .def("query_dependencies", &PyXPGraph::query_dependencies, py::arg("name"), py::arg("version") = "",
         py::arg("architecture") = "", py::arg("depth") = 1, py::arg("tree") = true, py::arg("use_gpu") = false,
         py::arg("satisfy_architecture") = true, py::arg("satisfy_version") = true,
         py::arg("expand_alternative") = true);
}
