#include "dependency_graph.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include "gpu_dependency_graph.hpp"

using DG = DependencyGraph;

DG::DependencyGraph() :
  gpu_graph_{new GpuDependencyGraph{*this}}, architectures{"native", "any", "all"}, dependency_types
  {"Depends", "Pre-Depends", "Recommends", "Suggests", "Breaks", "Conflicts", "Provides", "Replaces", "Enhances"} {}

DG::~DependencyGraph() { delete gpu_graph_; }

const DG::PackageNode &DG::create_package(std::string_view name) {
  auto [it, succ] = name_to_package_id_.try_emplace(std::string{name}, static_cast<PackageId>(package_nodes_.size()));
  return succ ? package_nodes_.emplace_back(std::string{name}) : package_nodes_[it->second];
}

const DG::VersionNode &DG::create_version(const PackageNode &pnode, std::string_view version, ArchitectureType arch) {
  for (auto vid : pnode.version_ids)
    if (const auto &vnode = version_nodes_[vid]; vnode.version == version && vnode.architecture == arch) return vnode;
  const_cast<PackageNode &>(pnode).version_ids.emplace_back(static_cast<VersionId>(version_nodes_.size()));
  return version_nodes_.emplace_back(std::string{version}, arch);
}

const DG::DependencyEdge &DG::create_dependency(const VersionNode &from, const PackageNode &to,
  std::string_view version_constr, ArchitectureType arch_constr, DependencyType dep_type, GroupId group) {
  const_cast<VersionNode &>(from).dependency_ids.emplace_back(static_cast<DependencyId>(dependency_edges_.size()));
  return dependency_edges_.emplace_back(id(from), id(to), std::string{version_constr}, arch_constr, dep_type, group);
}

DependencyItem DG::to_item(const DependencyEdge &dedge) const noexcept {
  return {
    package_nodes_[dedge.to_package_id].name, dependency_types.symbol(dedge.dependency_type),
    dedge.version_constraint, architectures.symbol(dedge.architecture_constraint),
  };
}

void DependencyGraph::build_gpu_graph(bool verbose) const { gpu_graph_->build(verbose); }

DependencyResult DG::query_dependencies(std::string_view name, std::string_view version, std::string_view arch,
  std::size_t depth, bool use_gpu) const {
  DependencyResult result{depth};
  std::vector<VersionId> frontier;
  if (auto it = name_to_package_id_.find(name); it != name_to_package_id_.end())
    for (auto vid : package_nodes_[it->second].version_ids) {
      const auto &vnode = version_nodes_[vid];
      if (!version.empty() && vnode.version != version) continue;
      if (!arch.empty() && architectures.symbol(vnode.architecture) != arch) continue;
      frontier.emplace_back(vid);
    }
  if (use_gpu) return gpu_graph_->query_dependencies(frontier, depth);

  std::unordered_set<VersionId> visited_vids{frontier.begin(), frontier.end()};
  for (auto level = 0; level < depth; level++) {
    std::unordered_set<DependencyItem> visited_direct_items;
    if (frontier.empty()) break;
    std::vector<VersionId> next;
    for (auto vid : frontier) {
      const auto &vnode = version_nodes_[vid];
      std::vector<DependencyGroup> curr_grps;
      std::vector<std::unordered_set<DependencyItem>> visited_grp_items;
      for (auto did : vnode.dependency_ids) {
        const auto &dedge = dependency_edges_[did];
        auto item = to_item(dedge);
        if (dedge.group > 0) {
          if (curr_grps.size() < dedge.group) {
            curr_grps.resize(dedge.group);
            visited_grp_items.resize(dedge.group);
          }
          if (visited_grp_items[dedge.group - 1].emplace(item).second)
            curr_grps[dedge.group - 1].emplace_back(std::move(item));
        } else if (visited_direct_items.emplace(item).second)
          result[level].direct_dependencies.emplace_back(std::move(item));

        if (level + 1 < depth && dependency_types.symbol(dedge.dependency_type) == "Depends" && dedge.group == 0) {
          const auto &dpnode = package_nodes_[dedge.to_package_id];
          for (auto next_vid : dpnode.version_ids) {
            if (visited_vids.contains(next_vid)) continue;
            const auto &next_vnode = version_nodes_[next_vid];
            bool match = false;
            if (architectures.symbol(dedge.architecture_constraint) == "native")
              match = next_vnode.architecture == vnode.architecture
                || architectures.symbol(next_vnode.architecture) == "all";
            else if (architectures.symbol(dedge.architecture_constraint) == "any") match = true;
            else match = next_vnode.architecture == dedge.architecture_constraint;
            if (match) {
              next.emplace_back(next_vid);
              visited_vids.emplace(next_vid);
            }
          }
        }
      }
      for (auto &grp : curr_grps) result[level].or_dependencies.emplace_back(std::move(grp));
    }
    frontier = std::move(next);
  }
  return result;
}
