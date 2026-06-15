#include "cache_graph.hpp"
#include <algorithm>
#include <cuda_runtime.h>
#include <memory>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include "storage_graph.hpp"

namespace xpg {

void cudaCheck(cudaError_t code) {
  if (code == cudaSuccess) return;
  throw std::runtime_error(cudaGetErrorString(code));
}

CacheGraph::CacheGraph(const StorageGraph &graph) noexcept
  : storage_graph_(graph), package_nodes_(nullptr), version_nodes_(nullptr), dependency_edges_(nullptr),
    frontier_(nullptr), frontier_trees_(nullptr), next_(nullptr), next_trees_(nullptr), next_size_(nullptr),
    result_(nullptr), result_trees_(nullptr), result_size_(nullptr), visited_(nullptr), mark_(1) {}

void CacheGraph::build() {
  clear();
  std::vector<PackageNode> pnodes;
  std::vector<VersionNode> vnodes;
  std::vector<DependencyEdge> dedges;
  to_cache_version_id_.resize(storage_graph_.version_nodes_.size());
  for (const auto &pnode : storage_graph_.package_nodes_) {
    auto vbegin = static_cast<VersionId>(vnodes.size());
    auto vcount = static_cast<VersionCount>(0);
    storage_graph_.for_each_version(pnode, [&, this](VersionId vid, const StorageGraph::VersionNode &vnode) {
      to_cache_version_id_[vid] = static_cast<VersionId>(vnodes.size());
      vnodes.push_back({vnode.architecture, vnode.dependency_count, static_cast<DependencyId>(dedges.size())});
      storage_graph_.for_each_dependency(vnode, [&](DependencyId did, const StorageGraph::DependencyEdge &dedge) {
        dedges.push_back({did, dedge.to_package, dedge.architecture_constraint, dedge.type, dedge.group});
      });
      ++vcount;
    });
    pnodes.push_back({vbegin, vcount});
  }
  cudaCheck(cudaMalloc(&package_nodes_, pnodes.size() * sizeof(PackageNode)));
  cudaCheck(cudaMalloc(&version_nodes_, vnodes.size() * sizeof(VersionNode)));
  cudaCheck(cudaMalloc(&dependency_edges_, dedges.size() * sizeof(DependencyEdge)));
  cudaCheck(cudaMalloc(&frontier_, kMaxDeviceVectorBytes));
  cudaCheck(cudaMalloc(&frontier_trees_, kMaxDeviceVectorBytes));
  cudaCheck(cudaMalloc(&next_, kMaxDeviceVectorBytes));
  cudaCheck(cudaMalloc(&next_trees_, kMaxDeviceVectorBytes));
  cudaCheck(cudaMalloc(&next_size_, sizeof(cuda_size_t)));
  cudaCheck(cudaMalloc(&result_, kMaxDeviceVectorBytes));
  cudaCheck(cudaMalloc(&result_trees_, kMaxDeviceVectorBytes));
  cudaCheck(cudaMalloc(&result_size_, sizeof(cuda_size_t)));
  cudaCheck(cudaMalloc(&visited_, vnodes.size() * sizeof(VisitedMark)));
  cudaCheck(cudaMemcpy(package_nodes_, pnodes.data(), pnodes.size() * sizeof(PackageNode), cudaMemcpyHostToDevice));
  cudaCheck(cudaMemcpy(version_nodes_, vnodes.data(), vnodes.size() * sizeof(VersionNode), cudaMemcpyHostToDevice));
  cudaCheck(cudaMemcpy(dependency_edges_, dedges.data(), dedges.size() * sizeof(DependencyEdge),
                       cudaMemcpyHostToDevice));
  cudaCheck(cudaMemset(visited_, 0, vnodes.size() * sizeof(VisitedMark)));
  mark_ = 1;
}

void CacheGraph::clear() {
  if (package_nodes_) cudaCheck(cudaFree(package_nodes_));
  if (version_nodes_) cudaCheck(cudaFree(version_nodes_));
  if (dependency_edges_) cudaCheck(cudaFree(dependency_edges_));
  if (frontier_) cudaCheck(cudaFree(frontier_));
  if (frontier_trees_) cudaCheck(cudaFree(frontier_trees_));
  if (next_) cudaCheck(cudaFree(next_));
  if (next_trees_) cudaCheck(cudaFree(next_trees_));
  if (next_size_) cudaCheck(cudaFree(next_size_));
  if (result_) cudaCheck(cudaFree(result_));
  if (result_trees_) cudaCheck(cudaFree(result_trees_));
  if (result_size_) cudaCheck(cudaFree(result_size_));
  if (visited_) cudaCheck(cudaFree(visited_));
  package_nodes_ = nullptr;
  version_nodes_ = nullptr;
  dependency_edges_ = nullptr;
  frontier_ = nullptr;
  frontier_trees_ = nullptr;
  next_ = nullptr;
  next_trees_ = nullptr;
  next_size_ = nullptr;
  result_ = nullptr;
  result_trees_ = nullptr;
  result_size_ = nullptr;
  visited_ = nullptr;
}

std::variant<DependencyTree, DependencyFlat> CacheGraph::query_dependencies(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree) const {
  if (tree) return query_dependency_tree(name, version, architecture, depth);
  return query_dependency_flat(name, version, architecture, depth);
}

std::vector<VersionId> CacheGraph::init_frontier(std::string_view name, std::string_view version,
                                                 std::string_view architecture) const {
  std::vector<VersionId> frontier = storage_graph_.init_frontier(name, version, architecture);
  for (auto &vid : frontier) vid = to_cache_version_id_[vid];
  return frontier;
}

__global__ void expand_tree(const CacheGraph::PackageNode *package_nodes, const CacheGraph::VersionNode *version_nodes,
                            const CacheGraph::DependencyEdge *dependency_edges, const VersionId *frontier,
                            const CacheGraph::TreeId *frontier_trees, cuda_size_t frontier_size, VersionId *next,
                            CacheGraph::TreeId *next_trees, cuda_size_t *next_size, DependencyId *result,
                            CacheGraph::TreeId *result_trees, cuda_size_t *result_size,
                            CacheGraph::VisitedMark *visited, CacheGraph::VisitedMark mark, cuda_size_t depth,
                            cuda_size_t level, cuda_size_t tree_size) {
  auto idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= frontier_size) return;
  if (level == 0) {
    auto old = visited[frontier[idx]];
    atomicCAS(&visited[frontier[idx]], old, mark);
  }
  const auto &vnode = version_nodes[frontier[idx]];
  for (auto did = vnode.dependency_begin; did < vnode.dependency_begin + vnode.dependency_count; ++did) {
    const auto &dedge = dependency_edges[did];
    auto rpos = atomicAdd(result_size, 1);
    if (rpos < kMaxDeviceVectorSize<DependencyId>) {
      result[rpos] = dedge.original;
      result_trees[rpos] = frontier_trees[idx];
    }
    if (level + 1 < depth && dedge.type == kDependsType && dedge.group == 0) {
      const auto &tpnode = package_nodes[dedge.to_package];
      for (auto vid = tpnode.version_begin; vid < tpnode.version_begin + tpnode.version_count; ++vid) {
        if (visited[vid] == mark) continue;
        const auto &vnode = version_nodes[vid];
        if (!StorageGraph::match_architecture(vnode.architecture, dedge.architecture_constraint)) continue;
        auto old = visited[vid];
        if (atomicCAS(&visited[vid], old, mark) != mark) {
          auto npos = atomicAdd(next_size, 1);
          if (npos < kMaxDeviceVectorSize<VersionId>) {
            next[npos] = vid;
            next_trees[npos] = tree_size + rpos;
          }
        }
      }
    }
  }
}

DependencyTree CacheGraph::query_dependency_tree(std::string_view name, std::string_view version,
                                                 std::string_view architecture, std::size_t depth) const {
  auto frontier = init_frontier(name, version, architecture);
  if (frontier.empty()) return DependencyTree();
  std::size_t frontier_size = frontier.size();
  struct BuildTreeNode {
    DependencyTree value;
    std::vector<TreeId> single_dependencies;
    std::vector<std::vector<TreeId>> alternative_dependencies;
  };
  std::vector<BuildTreeNode> trees(1);
  std::vector<TreeId> frontier_parents(frontier_size, 0);
  cudaCheck(cudaMemcpy(frontier_, frontier.data(), frontier_size * sizeof(VersionId), cudaMemcpyHostToDevice));
  cudaCheck(cudaMemcpy(frontier_trees_, frontier_parents.data(), frontier_parents.size() * sizeof(TreeId),
                       cudaMemcpyHostToDevice));
  for (auto level = 0; level < depth && frontier_size > 0; ++level) {
    cudaCheck(cudaMemset(next_size_, 0, sizeof(cuda_size_t)));
    cudaCheck(cudaMemset(result_size_, 0, sizeof(cuda_size_t)));
    int threads = 256, blocks = (frontier_size + threads - 1) / threads;
    cuda_size_t tree_size = trees.size();
    expand_tree<<<blocks, threads>>>(
      package_nodes_, version_nodes_, dependency_edges_, frontier_, frontier_trees_, frontier_size, next_, next_trees_,
      next_size_, result_, result_trees_, result_size_, visited_, mark_, depth, level, tree_size);
    cudaCheck(cudaGetLastError());
    cudaCheck(cudaDeviceSynchronize());
    cuda_size_t result_size;
    cudaCheck(cudaMemcpy(&result_size, result_size_, sizeof(cuda_size_t), cudaMemcpyDeviceToHost));
    if (result_size >= kMaxDeviceVectorSize<VersionId>) throw std::out_of_range("Reached max device vector size");
    std::vector<DependencyId> result_level(result_size);
    std::vector<TreeId> result_trees(result_size);
    cudaCheck(cudaMemcpy(result_level.data(), result_, result_size * sizeof(DependencyId), cudaMemcpyDeviceToHost));
    cudaCheck(cudaMemcpy(result_trees.data(), result_trees_, result_size * sizeof(TreeId),
                         cudaMemcpyDeviceToHost));

    std::unordered_map<TreeId, std::unordered_map<VersionId, std::vector<std::vector<TreeId>>>> groupsss;
    for (auto i : std::views::iota(0ull, result_size)) {
      const auto &dedge = storage_graph_.dependency_edges_[result_level[i]];
      const auto &pnode = storage_graph_.package_nodes_[dedge.to_package];
      auto &groups = groupsss[result_trees[i]][dedge.from_version];
      DependencyTree tree{
        storage_graph_.string_pool_[pnode.name], storage_graph_.dependency_types_[dedge.type],
        storage_graph_.string_pool_[dedge.version_constraint],
        storage_graph_.architectures_[dedge.architecture_constraint]
      };
      auto tid = static_cast<TreeId>(trees.size());
      trees.emplace_back(tree);
      if (dedge.group > 0) {
        if (dedge.group > groups.size()) groups.resize(dedge.group);
        groups[dedge.group - 1].emplace_back(tid);
      } else trees[result_trees[i]].single_dependencies.emplace_back(tid);
    }
    for (auto &[tid, groupss] : groupsss)
      for (auto &groups : groupss | std::views::values)
        for (auto &group : groups)
          if (!group.empty()) trees[tid].alternative_dependencies.emplace_back(std::move(group));
    if (level + 1 < depth) {
      cudaCheck(cudaMemcpy(&frontier_size, next_size_, sizeof(cuda_size_t), cudaMemcpyDeviceToHost));
      if (frontier_size >= kMaxDeviceVectorSize<VersionId>) throw std::out_of_range("Reached max device vector size");
      std::swap(frontier_, next_);
      std::swap(frontier_trees_, next_trees_);
    }
  }
  auto build_tree = [&](const auto &self, TreeId tid) -> DependencyTree {
    DependencyTree tree = std::move(trees[tid].value);
    for (auto subtid : trees[tid].single_dependencies) tree.single_dependencies.emplace_back(self(self, subtid));
    for (auto group : trees[tid].alternative_dependencies) {
      tree.alternative_dependencies.emplace_back();
      for (auto subtid : group) tree.alternative_dependencies.back().emplace_back(self(self, subtid));
    }
    return tree;
  };
  if (++mark_ == 0) {
    cudaCheck(cudaMemset(visited_, 0, to_cache_version_id_.size() * sizeof(VisitedMark)));
    mark_ = 1;
  }
  return build_tree(build_tree, 0);
}

__global__ void expand_flat(const CacheGraph::PackageNode *package_nodes, const CacheGraph::VersionNode *version_nodes,
                            const CacheGraph::DependencyEdge *dependency_edges, const VersionId *frontier,
                            cuda_size_t frontier_size, VersionId *next, cuda_size_t *next_size,
                            DependencyId *result, cuda_size_t *result_size, CacheGraph::VisitedMark *visited,
                            CacheGraph::VisitedMark mark, cuda_size_t depth, cuda_size_t level) {
  auto idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= frontier_size) return;
  if (level == 0) {
    auto old = visited[frontier[idx]];
    atomicCAS(&visited[frontier[idx]], old, mark);
  }
  const auto &vnode = version_nodes[frontier[idx]];
  for (auto did = vnode.dependency_begin; did < vnode.dependency_begin + vnode.dependency_count; ++did) {
    const auto &dedge = dependency_edges[did];
    auto pos = atomicAdd(result_size, 1);
    if (pos < kMaxDeviceVectorSize<DependencyId>) result[pos] = dedge.original;
    if (level + 1 < depth && dedge.type == kDependsType && dedge.group == 0) {
      const auto &pnode = package_nodes[dedge.to_package];
      for (auto vid = pnode.version_begin; vid < pnode.version_begin + pnode.version_count; ++vid) {
        if (visited[vid] == mark) continue;
        const auto &vnode = version_nodes[vid];
        if (!StorageGraph::match_architecture(vnode.architecture, dedge.architecture_constraint)) continue;
        auto old = visited[vid];
        if (atomicCAS(&visited[vid], old, mark) != mark) {
          pos = atomicAdd(next_size, 1);
          if (pos < kMaxDeviceVectorSize<VersionId>) next[pos] = vid;
        }
      }
    }
  }
}

DependencyFlat CacheGraph::query_dependency_flat(std::string_view name, std::string_view version,
                                                 std::string_view architecture, std::size_t depth) const {
  DependencyFlat result(depth);
  auto frontier = init_frontier(name, version, architecture);
  if (frontier.empty()) return result;
  cuda_size_t frontier_size = frontier.size();
  cudaCheck(cudaMemcpy(frontier_, frontier.data(), frontier_size * sizeof(VersionId), cudaMemcpyHostToDevice));
  for (auto level = 0; level < depth && frontier_size > 0; ++level) {
    cudaCheck(cudaMemset(next_size_, 0, sizeof(cuda_size_t)));
    cudaCheck(cudaMemset(result_size_, 0, sizeof(cuda_size_t)));
    int threads = 256, blocks = (frontier_size + threads - 1) / threads;
    expand_flat<<<blocks, threads>>>(package_nodes_, version_nodes_, dependency_edges_, frontier_, frontier_size,
                                     next_, next_size_, result_, result_size_, visited_, mark_, depth, level);
    cudaCheck(cudaGetLastError());
    cudaCheck(cudaDeviceSynchronize());
    cuda_size_t result_size;
    cudaCheck(cudaMemcpy(&result_size, result_size_, sizeof(cuda_size_t), cudaMemcpyDeviceToHost));
    if (result_size >= kMaxDeviceVectorSize<VersionId>) throw std::out_of_range("Reached max device vector size");
    std::vector<DependencyId> result_level(result_size);
    cudaCheck(cudaMemcpy(result_level.data(), result_, result_size * sizeof(DependencyId), cudaMemcpyDeviceToHost));

    std::unordered_map<VersionId, std::vector<std::vector<DependencyInfo>>> groupss;
    std::unordered_set<DependencyInfo> visited_dinfos;
    std::unordered_map<VersionId, std::vector<std::unordered_set<DependencyInfo>>> visited_ginfoss;
    for (auto did : result_level) {
      const auto &dedge = storage_graph_.dependency_edges_[did];
      const auto &pnode = storage_graph_.package_nodes_[dedge.to_package];
      auto &groups = groupss[dedge.from_version];
      auto &visited_ginfos = visited_ginfoss[dedge.from_version];
      DependencyInfo info{
        storage_graph_.string_pool_[pnode.name], storage_graph_.dependency_types_[dedge.type],
        storage_graph_.string_pool_[dedge.version_constraint],
        storage_graph_.architectures_[dedge.architecture_constraint]
      };
      if (dedge.group > 0) {
        if (dedge.group > groups.size()) {
          groups.resize(dedge.group);
          visited_ginfos.resize(dedge.group);
        }
        auto [it, succ] = visited_ginfos[dedge.group - 1].emplace(info);
        if (succ) groups[dedge.group - 1].emplace_back(std::move(info));
      } else {
        auto [it, succ] = visited_dinfos.emplace(info);
        if (succ) result[level].single_dependencies.emplace_back(std::move(info));
      }
    }
    for (auto &groups : groupss | std::views::values)
      for (auto &group : groups)
        if (!group.empty()) result[level].alternative_dependencies.emplace_back(std::move(group));
    if (level + 1 < depth) {
      cudaCheck(cudaMemcpy(&frontier_size, next_size_, sizeof(cuda_size_t), cudaMemcpyDeviceToHost));
      if (frontier_size >= kMaxDeviceVectorSize<VersionId>) throw std::out_of_range("Reached max device vector size");
      std::swap(frontier_, next_);
    }
  }
  if (++mark_ == 0) {
    cudaCheck(cudaMemset(visited_, 0, to_cache_version_id_.size() * sizeof(VisitedMark)));
    mark_ = 1;
  }
  return result;
}

} // namespace xpg
