#include <deque>
#include <exception>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <memgraph/mgp.hpp>
#include "config.hpp"
#include "types.hpp"

namespace xpg {

std::vector<mgp::Node> init_frontier(mgp_graph *graph, std::string_view name, std::string_view version,
                                     std::string_view architecture) {
  std::vector<mgp::Node> result;
  mgp::Map params;
  params.Insert("name", mgp::Value(name));
  params.Insert("version", mgp::Value(version));
  params.Insert("architecture", mgp::Value(architecture));
  using namespace std::string_view_literals;
  auto query_result = mgp::QueryExecution(graph).ExecuteQuery(
    "MATCH (:PackageNode {name: $name})-[:HAS_VERSION]->(v:VersionNode) WHERE ($version = '' OR v.version = $version) "
    "AND ($architecture = '' OR v.architecture = $architecture OR v.architecture = 'all' OR v.architecture = 'noarch') "
    "RETURN id(v) AS vid"sv, std::move(params));
  auto current_graph = mgp::Graph(graph);
  while (auto record = query_result.PullOne())
    result.emplace_back(current_graph.GetNodeById(mgp::Id::FromInt(record->At("vid").ValueInt())));
  return result;
}

DependencyTree query_dependency_tree(
  mgp_graph *graph, std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative, std::deque<std::string> &arena) {
  DependencyTree result;
  result.type = "";
  result.name = arena.emplace_back(name);
  result.version_constraint = arena.emplace_back(version);
  result.architecture_constraint = arena.emplace_back(architecture);
  auto frontier = init_frontier(graph, name, version, architecture);
  std::unordered_set visited(frontier.begin(), frontier.end());
  auto expand_tree = [&, architecture, depth]
  (const auto &self, const mgp::Node &vnode, DependencyTree &tree, std::size_t level) -> void {
    for (const auto &dedge : vnode.OutRelationships()) {
      if (dedge.Type() != "DEPENDS_ON") continue;
      const auto &pnode = dedge.To();
      if (!pnode.HasLabel("PackageNode")) continue;
      DependencyTree subtree;
      subtree.name = arena.emplace_back(pnode.GetProperty("name").ValueString());
      subtree.type = arena.emplace_back(dedge.GetProperty("type").ValueString());
      subtree.version_constraint = arena.emplace_back(dedge.GetProperty("version_constraint").ValueString());
      subtree.architecture_constraint = arena.emplace_back(dedge.GetProperty("architecture_constraint").ValueString());
      auto group = static_cast<DependencyGroupId>(dedge.GetProperty("group").ValueInt());
      if (level + 1 < depth && (expand_alternative || group == 0))
        for (const auto &vedge : pnode.OutRelationships()) {
          if (vedge.Type() != "HAS_VERSION") continue;
          const auto &vnode = vedge.To();
          if (!vnode.HasLabel("VersionNode")) continue;
          if (visited.contains(vnode)) continue;
          if (satisfy_architecture && !xpg::satisfy_architecture(
            vnode.GetProperty("architecture").ValueString(), subtree.architecture_constraint, architecture))
            continue;
          if (satisfy_version &&
            !xpg::satisfy_version(vnode.GetProperty("version").ValueString(), subtree.version_constraint)) { continue; }
          visited.insert(vnode);
          self(self, vnode, subtree, level + 1);
        }
      if (group != 0) {
        if (tree.alternative_dependencies.size() < group) tree.alternative_dependencies.resize(group);
        tree.alternative_dependencies[group - 1].emplace_back(std::move(subtree));
      } else tree.single_dependencies.emplace_back(std::move(subtree));
    }
  };
  for (const auto &node : frontier) expand_tree(expand_tree, node, result, 0);
  return result;
}

DependencyFlat query_dependency_flat(
  mgp_graph *graph, std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative, std::deque<std::string> &arena) {
  DependencyFlat result(depth);
  auto frontier = init_frontier(graph, name, version, architecture);
  std::unordered_set visited(frontier.begin(), frontier.end());
  for (auto level : std::views::iota(0ull, depth)) {
    std::vector<mgp::Node> next;
    std::unordered_set<DependencyInfo> dvisited;
    for (const auto &vnode : frontier) {
      std::vector<std::vector<DependencyInfo>> groups;
      std::vector<std::unordered_set<DependencyInfo>> gvisited;
      for (const auto &dedge : vnode.OutRelationships()) {
        if (dedge.Type() != "DEPENDS_ON") continue;
        const auto &pnode = dedge.To();
        if (!pnode.HasLabel("PackageNode")) continue;
        DependencyInfo info;
        info.name = arena.emplace_back(pnode.GetProperty("name").ValueString());
        info.type = arena.emplace_back(dedge.GetProperty("type").ValueString());
        info.version_constraint = arena.emplace_back(dedge.GetProperty("version_constraint").ValueString());
        info.architecture_constraint = arena.emplace_back(dedge.GetProperty("architecture_constraint").ValueString());
        auto version_constraint = info.version_constraint;
        auto architecture_constraint = info.architecture_constraint;
        auto group = static_cast<DependencyGroupId>(dedge.GetProperty("group").ValueInt());
        if (group > 0) {
          if (groups.size() < group) {
            groups.resize(group);
            gvisited.resize(group);
          }
          auto [it, succ] = gvisited[group - 1].emplace(info);
          if (succ) groups[group - 1].emplace_back(std::move(info));
        } else {
          auto [it, succ] = dvisited.emplace(info);
          if (succ) result[level].single_dependencies.emplace_back(std::move(info));
        }
        if (level + 1 < depth && (expand_alternative || group == 0)) {
          for (const auto &vedge : pnode.OutRelationships()) {
            if (vedge.Type() != "HAS_VERSION") continue;
            const auto &vnode = vedge.To();
            if (!vnode.HasLabel("VersionNode")) continue;
            if (visited.contains(vnode)) continue;
            if (satisfy_architecture && !xpg::satisfy_architecture(
              vnode.GetProperty("architecture").ValueString(), architecture_constraint, architecture))
              continue;
            if (satisfy_version &&
              !xpg::satisfy_version(vnode.GetProperty("version").ValueString(), version_constraint)) { continue; }
            next.emplace_back(vnode);
            visited.insert(vnode);
          }
        }
      }
      for (auto &group : groups)
        if (!group.empty()) result[level].alternative_dependencies.emplace_back(std::move(group));
    }
    frontier = std::move(next);
    if (frontier.empty()) break;
  }
  return result;
}

mgp::List to_mgp_list(const DependencyTree &tree) {
  mgp::List single(tree.single_dependencies.size());
  for (const auto &child : tree.single_dependencies) single.Append(mgp::Value(to_mgp_list(child)));
  mgp::List alternative(tree.alternative_dependencies.size());
  for (const auto &group : tree.alternative_dependencies) {
    mgp::List groupl(group.size());
    for (const auto &child : group) groupl.Append(mgp::Value(to_mgp_list(child)));
    alternative.Append(mgp::Value(std::move(groupl)));
  }
  mgp::List result(6);
  result.Append(mgp::Value(tree.name));
  result.Append(mgp::Value(tree.type));
  result.Append(mgp::Value(tree.version_constraint));
  result.Append(mgp::Value(tree.architecture_constraint));
  result.Append(mgp::Value(std::move(single)));
  result.Append(mgp::Value(std::move(alternative)));
  return result;
}

mgp::List to_mgp_list(const DependencyInfo &info) {
  mgp::List result(4);
  result.Append(mgp::Value(info.name));
  result.Append(mgp::Value(info.type));
  result.Append(mgp::Value(info.version_constraint));
  result.Append(mgp::Value(info.architecture_constraint));
  return result;
}

mgp::List to_mgp_list(const DependencyFlat &flat) {
  mgp::List result(flat.size());
  for (const auto &level : flat) {
    mgp::List single(level.single_dependencies.size());
    for (const auto &info : level.single_dependencies) single.Append(mgp::Value(to_mgp_list(info)));
    mgp::List alternative(level.alternative_dependencies.size());
    for (const auto &group : level.alternative_dependencies) {
      mgp::List groupl(group.size());
      for (const auto &info : group) groupl.Append(mgp::Value(to_mgp_list(info)));
      alternative.Append(mgp::Value(std::move(groupl)));
    }
    mgp::List levell(2);
    levell.Append(mgp::Value(std::move(single)));
    levell.Append(mgp::Value(std::move(alternative)));
    result.Append(mgp::Value(std::move(levell)));
  }
  return result;
}

void query_dependencies(mgp_list *args, mgp_graph *graph, mgp_result *result, mgp_memory *memory) {
  mgp::MemoryDispatcherGuard guard{memory};
  try {
    auto arguments = mgp::List(args);
    auto name = arguments[0].ValueString();
    auto version = arguments[1].ValueString();
    auto architecture = arguments[2].ValueString();
    auto raw_depth = arguments[3].ValueInt();
    if (raw_depth < 0) throw std::invalid_argument("depth must be non-negative");
    auto depth = static_cast<std::size_t>(raw_depth);
    auto tree = arguments[4].ValueBool();
    if (depth == 1) tree = false;
    auto satisfy_architecture = arguments[5].ValueBool();
    auto satisfy_version = arguments[6].ValueBool();
    auto expand_alternative = arguments[7].ValueBool();
    std::deque<std::string> arena;
    std::variant<DependencyTree, DependencyFlat> dresult;
    if (tree)
      dresult = query_dependency_tree(graph, name, version, architecture, depth, satisfy_architecture, satisfy_version,
                                      expand_alternative, arena);
    else
      dresult = query_dependency_flat(graph, name, version, architecture, depth, satisfy_architecture, satisfy_version,
                                      expand_alternative, arena);
    auto record = mgp::RecordFactory(result).NewRecord();
    if (tree) record.Insert("result", to_mgp_list(std::get<DependencyTree>(dresult)));
    else record.Insert("result", to_mgp_list(std::get<DependencyFlat>(dresult)));
  } catch (const std::exception &error) {
    mgp::RecordFactory(result).SetErrorMessage(error.what());
  } catch (...) { mgp::RecordFactory(result).SetErrorMessage("qmxpgraph query error"); }
}

} // namespace xpg

extern "C" int mgp_init_module(mgp_module *module, mgp_memory *memory) {
  try {
    mgp::MemoryDispatcherGuard guard(memory);
    mgp::AddProcedure(xpg::query_dependencies, "query_dependencies", mgp::ProcedureType::Read, {
                        mgp::Parameter("name", mgp::Type::String),
                        mgp::Parameter("version", mgp::Type::String),
                        mgp::Parameter("architecture", mgp::Type::String),
                        mgp::Parameter("depth", mgp::Type::Int),
                        mgp::Parameter("tree", mgp::Type::Bool),
                        mgp::Parameter("satisfy_architecture", mgp::Type::Bool),
                        mgp::Parameter("satisfy_version", mgp::Type::Bool),
                        mgp::Parameter("expand_alternative", mgp::Type::Bool)
                      }, {mgp::Return("result", mgp::Type::List)}, module, memory);
  } catch (const std::exception &) { return 1; }
  return 0;
}

extern "C" int mgp_shutdown_module() { return 0; }
