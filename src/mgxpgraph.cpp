#include "mgxpgraph.hpp"
#include <algorithm>
#include <cstdint>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xpg {

void from_mg_list(const mg::ConstList &value, DependencyTree &result, std::deque<std::string> &arena) {
  result.name = arena.emplace_back(value[0].ValueString());
  result.type = arena.emplace_back(value[1].ValueString());
  result.version_constraint = arena.emplace_back(value[2].ValueString());
  result.architecture_constraint = arena.emplace_back(value[3].ValueString());
  for (const auto child : value[4].ValueList())
    from_mg_list(child.ValueList(), result.single_dependencies.emplace_back(), arena);
  for (const auto vgroup : value[5].ValueList()) {
    auto &group = result.alternative_dependencies.emplace_back();
    for (const auto child : vgroup.ValueList()) from_mg_list(child.ValueList(), group.emplace_back(), arena);
  }
}

void from_mg_list(const mg::ConstList &value, DependencyInfo &result, std::deque<std::string> &arena) {
  result.name = arena.emplace_back(value[0].ValueString());
  result.type = arena.emplace_back(value[1].ValueString());
  result.version_constraint = arena.emplace_back(value[2].ValueString());
  result.architecture_constraint = arena.emplace_back(value[3].ValueString());
}

void from_mg_list(const mg::ConstList &value, DependencyFlat &result, std::deque<std::string> &arena) {
  result.reserve(value.size());
  for (const auto vlevel : value) {
    auto levell = vlevel.ValueList();
    auto &level = result.emplace_back();
    for (const auto info : levell[0].ValueList())
      from_mg_list(info.ValueList(), level.single_dependencies.emplace_back(), arena);
    for (const auto group_value : levell[1].ValueList()) {
      auto &group = level.alternative_dependencies.emplace_back();
      for (const auto info : group_value.ValueList()) from_mg_list(info.ValueList(), group.emplace_back(), arena);
    }
  }
}

void MGXPGraph::execute_query(std::string_view cypher) const {
  if (!client_) throw std::runtime_error("Memgraph is not connected");
  if (!client_->Execute(std::string(cypher)))
    throw std::runtime_error(std::format("Failed to execute query: {}", cypher));
}

void MGXPGraph::execute_query(std::string_view cypher, const mg::Map &params) const {
  if (!client_) throw std::runtime_error("Memgraph is not connected");
  if (!client_->Execute(std::string(cypher), params.AsConstMap()))
    throw std::runtime_error(std::format("Failed to execute query: {}", cypher));
}

std::optional<std::int64_t> MGXPGraph::query_int(std::string_view cypher) const {
  execute_query(cypher);
  auto record = client_->FetchOne();
  if (!record || record->empty()) {
    client_->DiscardAll();
    return std::nullopt;
  }
  auto result = record->front().ValueInt();
  client_->DiscardAll();
  return result;
}

std::optional<std::int64_t> MGXPGraph::query_int(std::string_view cypher, const mg::Map &params) const {
  execute_query(cypher, params);
  auto record = client_->FetchOne();
  if (!record || record->empty()) {
    client_->DiscardAll();
    return std::nullopt;
  }
  auto result = record->front().ValueInt();
  client_->DiscardAll();
  return result;
}

void MGXPGraph::connect(std::string_view host, std::uint16_t port, std::string_view username,
                        std::string_view password, bool use_ssl) {
  static struct init_t {
    init_t() { if (mg::Client::Init() != 0) throw std::runtime_error("Failed to initialize mgclient"); }
    ~init_t() { mg::Client::Finalize(); }
  } init;
  mg::Client::Params params;
  params.host = host;
  params.port = port;
  params.username = username;
  params.password = password;
  params.use_ssl = use_ssl;
  client_ = mg::Client::Connect(params);
  if (!client_) throw std::runtime_error("Failed to connect to Memgraph");
  execute_query("CREATE INDEX ON :PackageNode(name)");
  client_->DiscardAll();
}

void MGXPGraph::load_query_module() const {
  execute_query("CALL mg.load(\"qmxpgraph\")");
  client_->DiscardAll();
}

void MGXPGraph::clear() {
  execute_query("MATCH (n) DETACH DELETE n");
  client_->DiscardAll();
}

std::size_t MGXPGraph::package_count() const {
  return static_cast<std::size_t>(query_int("MATCH (p:PackageNode) RETURN count(p) AS count").value_or(0));
}

std::size_t MGXPGraph::version_count() const {
  return static_cast<std::size_t>(query_int("MATCH (v:VersionNode) RETURN count(v) AS count").value_or(0));
}

std::size_t MGXPGraph::dependency_count() const {
  return static_cast<std::size_t>(query_int("MATCH ()-[e:DEPENDS_ON]->() RETURN count(e) AS count").value_or(0));
}

PackageView MGXPGraph::get_package(PackageId pid) const {
  PackageView pview;
  mg::Map params(1);
  params.Insert("pid", mg::Value(static_cast<std::int64_t>(pid)));
  execute_query("MATCH (p) WHERE id(p) = $pid RETURN p.name", params);
  auto record = client_->FetchOne();
  pview.name = arena_.emplace_back(record->front().ValueString());
  client_->DiscardAll();
  pview.versions = [this, pid] {
    std::vector<VersionView> vviews;
    mg::Map params(1);
    params.Insert("pid", mg::Value(static_cast<std::int64_t>(pid)));
    execute_query("MATCH (p)-[:HAS_VERSION]->(v) WHERE id(p) = $pid RETURN id(v)", params);
    while (auto record = client_->FetchOne())
      vviews.emplace_back(get_version(static_cast<VersionId>(record->front().ValueInt())));
    return vviews;
  };
  return pview;
}

std::optional<PackageView> MGXPGraph::get_package(std::string_view name) const {
  PackageView pview;
  mg::Map params(1);
  params.Insert("name", mg::Value(name));
  auto result = query_int("MATCH (p:PackageNode {name: $name}) RETURN id(p)", params);
  if (!result) return std::nullopt;
  auto pid = static_cast<PackageId>(*result);
  pview.name = arena_.emplace_back(name);
  pview.versions = [this, pid] {
    std::vector<VersionView> vviews;
    mg::Map params(1);
    params.Insert("pid", mg::Value(static_cast<std::int64_t>(pid)));
    execute_query("MATCH (p)-[:HAS_VERSION]->(v) WHERE id(p) = $pid RETURN id(v)", params);
    while (auto record = client_->FetchOne())
      vviews.emplace_back(get_version(static_cast<VersionId>(record->front().ValueInt())));
    return vviews;
  };
  return pview;
}

VersionView MGXPGraph::get_version(VersionId vid) const {
  VersionView vview;
  mg::Map params(1);
  params.Insert("vid", mg::Value(static_cast<std::int64_t>(vid)));
  execute_query("MATCH (v) WHERE id(v) = $vid RETURN v.version, v.architecture", params);
  auto record = client_->FetchOne();
  vview.version = arena_.emplace_back(record->at(0).ValueString());
  vview.architecture = arena_.emplace_back(record->at(1).ValueString());
  client_->DiscardAll();
  vview.single_dependencies = [this, vid] {
    std::vector<DependencyView> dviews;
    mg::Map params(1);
    params.Insert("vid", mg::Value(static_cast<std::int64_t>(vid)));
    execute_query("MATCH (v)-[e:DEPENDS_ON]->() WHERE id(v) = $vid AND e.group = 0 RETURN id(e)", params);
    while (auto record = client_->FetchOne())
      dviews.emplace_back(get_dependency(static_cast<DependencyId>(record->front().ValueInt())));
    return dviews;
  };
  vview.alternative_dependencies = [this, vid] {
    std::vector<std::vector<DependencyView>> gviews;
    mg::Map params(1);
    params.Insert("vid", mg::Value(static_cast<std::int64_t>(vid)));
    execute_query("MATCH (v)-[e:DEPENDS_ON]->() WHERE id(v) = $vid AND e.group > 0 "
                  "RETURN e.group, id(e) ORDER BY e.group", params);
    while (auto record = client_->FetchOne()) {
      auto gid = static_cast<DependencyGroupId>(record->at(0).ValueInt());
      if (gviews.size() < gid) gviews.resize(gid);
      gviews[gid - 1].emplace_back(get_dependency(static_cast<DependencyId>(record->at(1).ValueInt())));
    }
    std::erase_if(gviews, [](const auto &gview) { return gview.empty(); });
    return gviews;
  };
  return vview;
}

DependencyView MGXPGraph::get_dependency(DependencyId did) const {
  DependencyView dview;
  mg::Map params(1);
  params.Insert("did", mg::Value(static_cast<std::int64_t>(did)));
  execute_query("MATCH (v)-[e:DEPENDS_ON]->(p) WHERE id(e) = $did "
                "RETURN id(v), id(p), e.type, e.version_constraint, e.architecture_constraint", params);
  auto record = client_->FetchOne();
  auto from_vid = static_cast<VersionId>(record->at(0).ValueInt());
  auto to_pid = static_cast<PackageId>(record->at(1).ValueInt());
  dview.type = arena_.emplace_back(record->at(2).ValueString());
  dview.version_constraint = arena_.emplace_back(record->at(3).ValueString());
  dview.architecture_constraint = arena_.emplace_back(record->at(4).ValueString());
  client_->DiscardAll();
  dview.from_version = [this, from_vid] { return get_version(from_vid); };
  dview.to_package = [this, to_pid] { return get_package(to_pid); };
  return dview;
}

bool MGXPGraph::create_package(const PackageInfo &info, bool update_if_exists) {
  if (!client_) throw std::runtime_error("Memgraph is not connected");
  if (!client_->BeginTransaction()) throw std::runtime_error("Failed to begin Memgraph transaction");
  try {
    auto [vid, vsucc] = create_version_node(info.name, info.version, info.architecture, update_if_exists);
    if (!vsucc) {
      if (!client_->CommitTransaction()) throw std::runtime_error("Failed to commit Memgraph transaction");
      return false;
    }
    for (const auto &dinfo : info.single_dependencies)
      create_dependency_edge(vid, dinfo.name, dinfo.version_constraint, dinfo.architecture_constraint, dinfo.type, 0);
    auto gid = static_cast<DependencyGroupId>(1);
    for (const auto &group : info.alternative_dependencies) {
      for (const auto &ginfo : group)
        create_dependency_edge(vid, ginfo.name, ginfo.version_constraint, ginfo.architecture_constraint, ginfo.type,
                               gid);
      ++gid;
    }
    if (!client_->CommitTransaction()) throw std::runtime_error("Failed to commit Memgraph transaction");
    return true;
  } catch (...) {
    client_->RollbackTransaction();
    throw;
  }
}

bool MGXPGraph::delete_package(std::string_view name, std::string_view version, std::string_view architecture) {
  mg::Map params(3);
  params.Insert("name", mg::Value(name));
  params.Insert("version", mg::Value(version));
  params.Insert("architecture", mg::Value(architecture));
  return query_int(
    "MATCH (p:PackageNode {name: $name})-[:HAS_VERSION]->(v:VersionNode) "
    "WHERE ($version = '' OR v.version = $version) AND ($architecture = '' OR v.architecture = $architecture) "
    "WITH collect(v) AS versions FOREACH (v IN versions | DETACH DELETE v) "
    "RETURN size(versions)", params).value_or(0) > 0;
}

std::pair<PackageId, bool> MGXPGraph::create_package_node(std::string_view name) {
  mg::Map params(1);
  params.Insert("name", mg::Value(name));
  auto pid = query_int("MATCH (p:PackageNode {name: $name}) RETURN id(p)", params);
  if (pid) return {static_cast<PackageId>(*pid), false};
  pid = query_int("CREATE (p:PackageNode {name: $name}) RETURN id(p)", params);
  if (!pid) throw std::runtime_error("Package node creation returned no ID");
  return {static_cast<PackageId>(*pid), true};
}

std::pair<VersionId, bool> MGXPGraph::create_version_node(PackageId pid, std::string_view version,
                                                          std::string_view arch, bool update_if_exists) {
  mg::Map params(4);
  params.Insert("pid", mg::Value(static_cast<std::int64_t>(pid)));
  params.Insert("version", mg::Value(version));
  params.Insert("architecture", mg::Value(arch));
  auto vid = query_int("MATCH (p) WHERE id(p) = $pid "
                       "MATCH (p)-[:HAS_VERSION]->(v:VersionNode {version: $version, architecture: $architecture}) "
                       "RETURN id(v)", params);
  return create_version_node(pid, std::optional<VersionId>(vid), version, arch, update_if_exists);
}

std::pair<VersionId, bool> MGXPGraph::create_version_node(std::string_view name, std::string_view version,
                                                          std::string_view arch, bool update_if_exists) {
  mg::Map params(4);
  params.Insert("name", mg::Value(name));
  params.Insert("version", mg::Value(version));
  params.Insert("architecture", mg::Value(arch));
  execute_query("MERGE (p:PackageNode {name: $name}) WITH p "
                "OPTIONAL MATCH (p)-[:HAS_VERSION]->(v:VersionNode {version: $version, architecture: $architecture}) "
                "RETURN id(p), id(v)", params);
  std::optional<VersionId> vid;
  auto record = client_->FetchOne();
  auto pid = static_cast<PackageId>(record->at(0).ValueInt());
  if (record->at(1).type() != mg::Value::Type::Null) vid = record->at(1).ValueInt();
  client_->DiscardAll();
  return create_version_node(pid, vid, version, arch, update_if_exists);
}

std::pair<VersionId, bool> MGXPGraph::create_version_node(PackageId pid, std::optional<VersionId> old_vid,
                                                          std::string_view version, std::string_view arch,
                                                          bool update_if_exists) {
  mg::Map params(4);
  params.Insert("pid", mg::Value(static_cast<std::int64_t>(pid)));
  params.Insert("version", mg::Value(version));
  params.Insert("architecture", mg::Value(arch));
  std::optional<std::int64_t> vid;
  if (!old_vid) {
    vid = query_int("MATCH (p) WHERE id(p) = $pid "
                    "CREATE (p)-[:HAS_VERSION]->(v:VersionNode {version: $version, architecture: $architecture}) "
                    "RETURN id(v)", params);
  } else if (update_if_exists) {
    params.Insert("vid", mg::Value(static_cast<std::int64_t>(*old_vid)));
    vid = query_int("MATCH (old) WHERE id(old) = $vid DETACH DELETE old WITH 1 AS _ MATCH (p) WHERE id(p) = $pid "
                    "CREATE (p)-[:HAS_VERSION]->(v:VersionNode {version: $version, architecture: $architecture}) "
                    "RETURN id(v)", params);
  } else return {*old_vid, false};
  if (!vid) throw std::runtime_error("Version node creation returned no ID");
  return {static_cast<VersionId>(*vid), true};
}

std::pair<DependencyId, bool> MGXPGraph::create_dependency_edge(VersionId from_vid, PackageId to_pid,
                                                                std::string_view vcons, std::string_view acons,
                                                                std::string_view type, DependencyGroupId group) {
  mg::Map params(6);
  params.Insert("vid", mg::Value(static_cast<std::int64_t>(from_vid)));
  params.Insert("pid", mg::Value(static_cast<std::int64_t>(to_pid)));
  params.Insert("type", mg::Value(type));
  params.Insert("version_constraint", mg::Value(vcons));
  params.Insert("architecture_constraint", mg::Value(acons));
  params.Insert("group", mg::Value(static_cast<std::int64_t>(group)));
  auto did = query_int("MATCH (v) WHERE id(v) = $vid MATCH (p) WHERE id(p) = $pid "
                       "CREATE (v)-[e:DEPENDS_ON {type: $type, version_constraint: $version_constraint, "
                       "architecture_constraint: $architecture_constraint, group: $group}]->(p) "
                       "RETURN id(e)", params);
  if (!did) throw std::runtime_error("Dependency edge creation returned no ID");
  return {static_cast<DependencyId>(*did), true};
}

std::pair<DependencyId, bool> MGXPGraph::create_dependency_edge(VersionId from_vid, std::string_view to_pname,
                                                                std::string_view vcons, std::string_view acons,
                                                                std::string_view type, DependencyGroupId group) {
  mg::Map params(6);
  params.Insert("vid", mg::Value(static_cast<std::int64_t>(from_vid)));
  params.Insert("name", mg::Value(to_pname));
  params.Insert("type", mg::Value(type));
  params.Insert("version_constraint", mg::Value(vcons));
  params.Insert("architecture_constraint", mg::Value(acons));
  params.Insert("group", mg::Value(static_cast<std::int64_t>(group)));
  auto did = query_int("MERGE (p:PackageNode {name: $name}) WITH p MATCH (v) WHERE id(v) = $vid "
                       "CREATE (v)-[e:DEPENDS_ON {type: $type, version_constraint: $version_constraint, "
                       "architecture_constraint: $architecture_constraint, group: $group}]->(p) "
                       "RETURN id(e)", params);
  if (!did) throw std::runtime_error("Dependency edge creation returned no ID");
  return {static_cast<DependencyId>(*did), true};
}

std::vector<std::string_view> MGXPGraph::query_packages(std::string_view architecture,
                                                        std::string_view prefix) const {
  mg::Map params(2);
  params.Insert("architecture", mg::Value(architecture));
  params.Insert("prefix", mg::Value(prefix));
  execute_query(
    "MATCH (p:PackageNode)-[:HAS_VERSION]->(v:VersionNode) WHERE ($prefix = '' OR p.name STARTS WITH $prefix) "
    "AND ($architecture = '' OR v.architecture = $architecture OR v.architecture = 'all' OR v.architecture = 'noarch') "
    "WITH p.name AS name, count(v) AS vcount RETURN name ORDER BY name", params);
  std::vector<std::string_view> result;
  while (auto record = client_->FetchOne()) result.emplace_back(arena_.emplace_back(record->at(0).ValueString()));
  return result;
}

std::vector<VersionInfo> MGXPGraph::query_versions(std::string_view name, std::string_view architecture) const {
  mg::Map params(2);
  params.Insert("name", mg::Value(name));
  params.Insert("architecture", mg::Value(architecture));
  execute_query(
    "MATCH (:PackageNode {name: $name})-[:HAS_VERSION]->(v:VersionNode) "
    "WHERE $architecture = '' OR v.architecture = $architecture OR v.architecture = 'all' OR v.architecture = 'noarch' "
    "RETURN v.version, v.architecture", params);
  std::vector<VersionInfo> versions;
  while (auto record = client_->FetchOne()) {
    auto &vinfo = versions.emplace_back();
    vinfo.version = arena_.emplace_back(record->at(0).ValueString());
    vinfo.architecture = arena_.emplace_back(record->at(1).ValueString());
  }
  return versions;
}

std::variant<DependencyTree, DependencyFlat> MGXPGraph::query_dependencies(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree,
  bool use_query_modules, bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  if (depth == 1) tree = false;
  if (use_query_modules)
    return query_dependencies_use_query_modules(name, version, architecture, depth, tree, satisfy_architecture,
                                                satisfy_version, expand_alternative);
  if (tree)
    return query_dependency_tree(name, version, architecture, depth, satisfy_architecture, satisfy_version,
                                 expand_alternative);
  return query_dependency_flat(name, version, architecture, depth, satisfy_architecture, satisfy_version,
                               expand_alternative);
}

std::vector<VersionId> MGXPGraph::init_frontier(std::string_view name, std::string_view version,
                                                std::string_view architecture) const {
  std::vector<VersionId> frontier;
  mg::Map params(3);
  params.Insert("name", mg::Value(name));
  params.Insert("version", mg::Value(version));
  params.Insert("architecture", mg::Value(architecture));
  execute_query(
    "MATCH (:PackageNode {name: $name})-[:HAS_VERSION]->(v:VersionNode) WHERE ($version = '' OR v.version = $version) "
    "AND ($architecture = '' OR v.architecture = $architecture OR v.architecture = 'all' OR v.architecture = 'noarch') "
    "RETURN id(v)", params);
  while (auto record = client_->FetchOne()) frontier.emplace_back(static_cast<VersionId>(record->at(0).ValueInt()));
  return frontier;
}

DependencyTree MGXPGraph::query_dependency_tree(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  DependencyTree result;
  result.name = arena_.emplace_back(name);
  result.version_constraint = arena_.emplace_back(version);
  result.architecture_constraint = arena_.emplace_back(architecture);
  auto frontier = init_frontier(name, version, architecture);
  std::unordered_set visited(frontier.begin(), frontier.end());
  auto expand_tree = [&, this, depth]
  (const auto &self, VersionId vid, DependencyTree &tree, std::size_t level) -> void {
    std::vector<DependencyEdge> dedges;
    mg::Map params(1);
    params.Insert("vid", mg::Value(static_cast<std::int64_t>(vid)));
    execute_query("MATCH (v)-[e:DEPENDS_ON]->(p:PackageNode) WHERE id(v) = $vid "
                  "RETURN id(p), p.name, e.type, e.version_constraint, e.architecture_constraint, e.group", params);
    while (auto record = client_->FetchOne()) {
      auto &dedge = dedges.emplace_back();
      dedge.to_package = static_cast<PackageId>(record->at(0).ValueInt());
      dedge.name = arena_.emplace_back(record->at(1).ValueString());
      dedge.type = arena_.emplace_back(record->at(2).ValueString());
      dedge.version_constraint = arena_.emplace_back(record->at(3).ValueString());
      dedge.architecture_constraint = arena_.emplace_back(record->at(4).ValueString());
      dedge.group = static_cast<DependencyGroupId>(record->at(5).ValueInt());
    }
    for (const auto &dedge : dedges) {
      DependencyTree subtree;
      subtree.name = dedge.name;
      subtree.type = dedge.type;
      subtree.version_constraint = dedge.version_constraint;
      subtree.architecture_constraint = dedge.architecture_constraint;
      if (level + 1 < depth && (expand_alternative || dedge.group == 0)) {
        std::vector<VersionNode> vnodes;
        mg::Map params(1);
        params.Insert("pid", mg::Value(static_cast<std::int64_t>(dedge.to_package)));
        execute_query("MATCH (p)-[:HAS_VERSION]->(v:VersionNode) WHERE id(p) = $pid "
                      "RETURN id(v), v.version, v.architecture", params);
        while (auto record = client_->FetchOne()) {
          auto &vnode = vnodes.emplace_back();
          vnode.id = static_cast<VersionId>(record->at(0).ValueInt());
          vnode.version = arena_.emplace_back(record->at(1).ValueString());
          vnode.architecture = arena_.emplace_back(record->at(2).ValueString());
        }
        for (const auto &vnode : vnodes) {
          if (visited.contains(vnode.id)) continue;
          if (satisfy_architecture &&
            !xpg::satisfy_architecture(vnode.architecture, dedge.architecture_constraint, architecture)) { continue; }
          if (satisfy_version && !xpg::satisfy_version(vnode.version, dedge.version_constraint)) continue;
          visited.insert(vnode.id);
          self(self, vnode.id, subtree, level + 1);
        }
      }
      if (dedge.group != 0) {
        if (tree.alternative_dependencies.size() < dedge.group) tree.alternative_dependencies.resize(dedge.group);
        tree.alternative_dependencies[dedge.group - 1].emplace_back(std::move(subtree));
      } else tree.single_dependencies.emplace_back(std::move(subtree));
    }
  };
  for (auto vid : frontier) expand_tree(expand_tree, vid, result, 0);
  return result;
}

DependencyFlat MGXPGraph::query_dependency_flat(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  DependencyFlat result(depth);
  auto frontier = init_frontier(name, version, architecture);
  std::unordered_set visited(frontier.begin(), frontier.end());
  for (auto level : std::views::iota(0ull, depth)) {
    std::vector<VersionId> next;
    std::unordered_set<DependencyInfo> dvisited;
    for (auto vid : frontier) {
      std::vector<DependencyEdge> dedges;
      std::vector<std::vector<DependencyInfo>> groups;
      std::vector<std::unordered_set<DependencyInfo>> gvisited;
      mg::Map params(1);
      params.Insert("vid", mg::Value(static_cast<std::int64_t>(vid)));
      execute_query("MATCH (v)-[e:DEPENDS_ON]->(p:PackageNode) WHERE id(v) = $vid "
                    "RETURN id(p), p.name, e.type, e.version_constraint, e.architecture_constraint, e.group", params);
      while (auto record = client_->FetchOne()) {
        auto &dedge = dedges.emplace_back();
        dedge.to_package = static_cast<PackageId>(record->at(0).ValueInt());
        dedge.name = arena_.emplace_back(record->at(1).ValueString());
        dedge.type = arena_.emplace_back(record->at(2).ValueString());
        dedge.version_constraint = arena_.emplace_back(record->at(3).ValueString());
        dedge.architecture_constraint = arena_.emplace_back(record->at(4).ValueString());
        dedge.group = static_cast<DependencyGroupId>(record->at(5).ValueInt());
      }
      for (const auto &dedge : dedges) {
        DependencyInfo info;
        info.name = dedge.name;
        info.type = dedge.type;
        info.version_constraint = dedge.version_constraint;
        info.architecture_constraint = dedge.architecture_constraint;
        if (dedge.group > 0) {
          if (groups.size() < dedge.group) {
            groups.resize(dedge.group);
            gvisited.resize(dedge.group);
          }
          auto [it, succ] = gvisited[dedge.group - 1].emplace(info);
          if (succ) groups[dedge.group - 1].emplace_back(std::move(info));
        } else {
          auto [it, succ] = dvisited.emplace(info);
          if (succ) result[level].single_dependencies.emplace_back(std::move(info));
        }
        if (level + 1 < depth && (expand_alternative || dedge.group == 0)) {
          std::vector<VersionNode> vnodes;
          mg::Map params(1);
          params.Insert("pid", mg::Value(static_cast<std::int64_t>(dedge.to_package)));
          execute_query("MATCH (p)-[:HAS_VERSION]->(v:VersionNode) WHERE id(p) = $pid "
                        "RETURN id(v), v.version, v.architecture", params);
          while (auto record = client_->FetchOne()) {
            auto &vnode = vnodes.emplace_back();
            vnode.id = static_cast<VersionId>(record->at(0).ValueInt());
            vnode.version = arena_.emplace_back(record->at(1).ValueString());
            vnode.architecture = arena_.emplace_back(record->at(2).ValueString());
          }
          for (const auto &vnode : vnodes) {
            if (visited.contains(vnode.id)) continue;
            if (satisfy_architecture &&
              !xpg::satisfy_architecture(vnode.architecture, dedge.architecture_constraint, architecture)) { continue; }
            if (satisfy_version && !xpg::satisfy_version(vnode.version, dedge.version_constraint)) continue;
            next.emplace_back(vnode.id);
            visited.emplace(vnode.id);
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

std::variant<DependencyTree, DependencyFlat> MGXPGraph::query_dependencies_use_query_modules(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  if (depth == 1) tree = false;
  mg::Map params(8);
  params.Insert("name", mg::Value(name));
  params.Insert("version", mg::Value(version));
  params.Insert("architecture", mg::Value(architecture));
  params.Insert("depth", mg::Value(static_cast<std::int64_t>(depth)));
  params.Insert("tree", mg::Value(tree));
  params.Insert("satisfy_architecture", mg::Value(satisfy_architecture));
  params.Insert("satisfy_version", mg::Value(satisfy_version));
  params.Insert("expand_alternative", mg::Value(expand_alternative));
  execute_query("CALL qmxpgraph.query_dependencies($name, $version, $architecture, $depth, $tree, "
                "$satisfy_architecture, $satisfy_version, $expand_alternative) YIELD result RETURN result", params);
  auto record = client_->FetchOne();
  if (!record || record->empty()) throw std::runtime_error("qmxpgraph.query_dependencies returned no rows");
  try {
    auto encoded = record->front().AsConstValue().ValueList();
    std::variant<DependencyTree, DependencyFlat> result;
    if (tree) {
      result.emplace<DependencyTree>();
      from_mg_list(encoded, std::get<DependencyTree>(result), arena_);
    } else {
      result.emplace<DependencyFlat>();
      from_mg_list(encoded, std::get<DependencyFlat>(result), arena_);
    }
    client_->DiscardAll();
    return result;
  } catch (...) {
    client_->DiscardAll();
    throw;
  }
}

} // namespace xpg
