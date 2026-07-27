#include "cuda_cache.hpp"
#include <cuda_runtime.h>
#include <memory>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "xpgraph.hpp"

namespace xpg {

void cudaCheck(cudaError_t code) {
  if (code == cudaSuccess) return;
  throw std::runtime_error(cudaGetErrorString(code));
}

CudaCache::CudaCache(const XPGraph &graph) noexcept
  : graph_(graph), package_nodes_(nullptr), version_nodes_(nullptr), dependency_edges_(nullptr),
    string_pool_(nullptr), frontier_(nullptr), frontier_trees_(nullptr), next_(nullptr), next_trees_(nullptr),
    next_size_(nullptr), result_(nullptr), result_trees_(nullptr), result_size_(nullptr), visited_(nullptr), mark_(1) {}

CudaCache::CudaCache(CudaCache &&other) noexcept
  : graph_(other.graph_), to_cache_version_id_(std::move(other.to_cache_version_id_)),
    package_nodes_(std::exchange(other.package_nodes_, nullptr)),
    version_nodes_(std::exchange(other.version_nodes_, nullptr)),
    dependency_edges_(std::exchange(other.dependency_edges_, nullptr)),
    string_pool_(std::exchange(other.string_pool_, nullptr)), frontier_(std::exchange(other.frontier_, nullptr)),
    frontier_trees_(std::exchange(other.frontier_trees_, nullptr)), next_(std::exchange(other.next_, nullptr)),
    next_trees_(std::exchange(other.next_trees_, nullptr)), next_size_(std::exchange(other.next_size_, nullptr)),
    result_(std::exchange(other.result_, nullptr)), result_trees_(std::exchange(other.result_trees_, nullptr)),
    result_size_(std::exchange(other.result_size_, nullptr)), visited_(std::exchange(other.visited_, nullptr)),
    mark_(other.mark_) {}

CudaCache &CudaCache::operator=(CudaCache &&other) noexcept {
  if (this == &other) return *this;
  clear();
  to_cache_version_id_ = std::move(other.to_cache_version_id_);
  package_nodes_ = std::exchange(other.package_nodes_, nullptr);
  version_nodes_ = std::exchange(other.version_nodes_, nullptr);
  dependency_edges_ = std::exchange(other.dependency_edges_, nullptr);
  string_pool_ = std::exchange(other.string_pool_, nullptr);
  frontier_ = std::exchange(other.frontier_, nullptr);
  frontier_trees_ = std::exchange(other.frontier_trees_, nullptr);
  next_ = std::exchange(other.next_, nullptr);
  next_trees_ = std::exchange(other.next_trees_, nullptr);
  next_size_ = std::exchange(other.next_size_, nullptr);
  result_ = std::exchange(other.result_, nullptr);
  result_trees_ = std::exchange(other.result_trees_, nullptr);
  result_size_ = std::exchange(other.result_size_, nullptr);
  visited_ = std::exchange(other.visited_, nullptr);
  mark_ = other.mark_;
  return *this;
}

void CudaCache::build() {
  clear();
  std::vector<PackageNode> cpnodes;
  std::vector<VersionNode> cvnodes;
  std::vector<DependencyEdge> cdedges;
  to_cache_version_id_.resize(graph_.version_nodes_.size());
  for (const auto &pnode : graph_.package_nodes_) {
    auto cvbegin = static_cast<VersionId>(cvnodes.size());
    auto cvcount = static_cast<VersionCount>(0);
    graph_.for_each_version(pnode, [&, this](VersionId vid, const XPGraph::VersionNode &vnode) {
      to_cache_version_id_[vid] = static_cast<VersionId>(cvnodes.size());
      auto version = graph_.string_pool_[vnode.version];
      auto &cvnode = cvnodes.emplace_back();
      cvnode.architecture = vnode.architecture;
      cvnode.version_id = static_cast<StringId>(vnode.version + sizeof(StringLength));
      cvnode.version_length = static_cast<StringLength>(version.size());
      cvnode.dependency_begin = static_cast<DependencyId>(cdedges.size());
      cvnode.dependency_count = vnode.dependency_count;
      graph_.for_each_dependency(vnode, [&](DependencyId did, const XPGraph::DependencyEdge &dedge) {
        auto version_constraint = graph_.string_pool_[dedge.version_constraint];
        auto &cdedge = cdedges.emplace_back();
        cdedge.original = did;
        cdedge.to_package = dedge.to_package;
        cdedge.version_constraint_id = static_cast<StringId>(dedge.version_constraint + sizeof(StringLength));
        cdedge.version_constraint_length = static_cast<StringLength>(version_constraint.size());
        cdedge.architecture_constraint = dedge.architecture_constraint;
        cdedge.type = dedge.type;
        cdedge.group = dedge.group;
      });
      ++cvcount;
    });
    auto &cpnode = cpnodes.emplace_back();
    cpnode.version_begin = cvbegin;
    cpnode.version_count = cvcount;
  }
  cudaCheck(cudaMalloc(&package_nodes_, cpnodes.size() * sizeof(PackageNode)));
  cudaCheck(cudaMalloc(&version_nodes_, cvnodes.size() * sizeof(VersionNode)));
  cudaCheck(cudaMalloc(&dependency_edges_, cdedges.size() * sizeof(DependencyEdge)));
  cudaCheck(cudaMalloc(&string_pool_, graph_.string_pool_.size_bytes()));
  cudaCheck(cudaMalloc(&frontier_, kDeviceVectorBytes));
  cudaCheck(cudaMalloc(&frontier_trees_, kDeviceVectorBytes));
  cudaCheck(cudaMalloc(&next_, kDeviceVectorBytes));
  cudaCheck(cudaMalloc(&next_trees_, kDeviceVectorBytes));
  cudaCheck(cudaMalloc(&next_size_, sizeof(cuda_size_t)));
  cudaCheck(cudaMalloc(&result_, kDeviceVectorBytes));
  cudaCheck(cudaMalloc(&result_trees_, kDeviceVectorBytes));
  cudaCheck(cudaMalloc(&result_size_, sizeof(cuda_size_t)));
  cudaCheck(cudaMalloc(&visited_, cvnodes.size() * sizeof(VisitedMark)));
  cudaCheck(cudaMemcpy(package_nodes_, cpnodes.data(), cpnodes.size() * sizeof(PackageNode), cudaMemcpyHostToDevice));
  cudaCheck(cudaMemcpy(version_nodes_, cvnodes.data(), cvnodes.size() * sizeof(VersionNode), cudaMemcpyHostToDevice));
  cudaCheck(cudaMemcpy(dependency_edges_, cdedges.data(), cdedges.size() * sizeof(DependencyEdge),
                       cudaMemcpyHostToDevice));
  cudaCheck(cudaMemcpy(string_pool_, graph_.string_pool_.data(), graph_.string_pool_.size_bytes(),
                       cudaMemcpyHostToDevice));
  cudaCheck(cudaMemset(visited_, 0, cvnodes.size() * sizeof(VisitedMark)));
  mark_ = 1;
}

void CudaCache::clear() {
  if (package_nodes_) cudaCheck(cudaFree(package_nodes_));
  if (version_nodes_) cudaCheck(cudaFree(version_nodes_));
  if (dependency_edges_) cudaCheck(cudaFree(dependency_edges_));
  if (string_pool_) cudaCheck(cudaFree(string_pool_));
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
  string_pool_ = nullptr;
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

std::variant<DependencyTree, DependencyFlat> CudaCache::query_dependencies(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  if (tree)
    return query_dependency_tree(name, version, architecture, depth, satisfy_architecture, satisfy_version,
                                 expand_alternative);
  return query_dependency_flat(name, version, architecture, depth, satisfy_architecture, satisfy_version,
                               expand_alternative);
}

std::vector<VersionId> CudaCache::init_frontier(std::string_view name, std::string_view version,
                                                ArchitectureType arch) const {
  std::vector<VersionId> frontier = graph_.init_frontier(name, version, arch);
  for (auto &vid : frontier) vid = to_cache_version_id_[vid];
  return frontier;
}

__global__ void expand_tree(const CudaCache::PackageNode *package_nodes, const CudaCache::VersionNode *version_nodes,
                            const CudaCache::DependencyEdge *dependency_edges, const char *string_pool,
                            const VersionId *frontier, const CudaCache::TreeId *frontier_trees,
                            cuda_size_t frontier_size, VersionId *next, CudaCache::TreeId *next_trees,
                            cuda_size_t *next_size, DependencyId *result, CudaCache::TreeId *result_trees,
                            cuda_size_t *result_size, CudaCache::VisitedMark *visited, CudaCache::VisitedMark mark,
                            cuda_size_t depth, cuda_size_t level, cuda_size_t tree_size, ArchitectureType source,
                            bool satisfy_architecture, bool satisfy_version, bool expand_alternative) {
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
    if (rpos < kDeviceVectorSize<DependencyId>) {
      result[rpos] = dedge.original;
      result_trees[rpos] = frontier_trees[idx];
    }
    if (level + 1 < depth && (expand_alternative || dedge.group == 0)) {
      const auto &pnode = package_nodes[dedge.to_package];
      for (auto vid = pnode.version_begin; vid < pnode.version_begin + pnode.version_count; ++vid) {
        if (visited[vid] == mark) continue;
        const auto &next_vnode = version_nodes[vid];
        if (satisfy_architecture &&
          !xpg::satisfy_architecture(next_vnode.architecture, dedge.architecture_constraint, source)) { continue; }
        if (satisfy_version && !xpg::satisfy_version(
          {string_pool + next_vnode.version_id, next_vnode.version_length},
          {string_pool + dedge.version_constraint_id, dedge.version_constraint_length})) { continue; }
        auto old = visited[vid];
        if (atomicCAS(&visited[vid], old, mark) != mark) {
          auto npos = atomicAdd(next_size, 1);
          if (npos < kDeviceVectorSize<VersionId>) {
            next[npos] = vid;
            next_trees[npos] = tree_size + rpos;
          }
        }
      }
    }
  }
}

DependencyTree CudaCache::query_dependency_tree(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  std::vector<BuildTreeNode> trees(1);
  trees[0].value.name = name;
  trees[0].value.version_constraint = version;
  trees[0].value.architecture_constraint = architecture;
  auto arch = graph_.architecture_types_.id(architecture).value_or(ArchitectureType::kNull);
  auto frontier = init_frontier(name, version, arch);
  if (frontier.empty()) return trees[0].value;
  cuda_size_t frontier_size = frontier.size();
  std::vector<TreeId> frontier_parents(frontier_size, 0);
  cudaCheck(cudaMemcpy(frontier_, frontier.data(), frontier_size * sizeof(VersionId), cudaMemcpyHostToDevice));
  cudaCheck(cudaMemcpy(frontier_trees_, frontier_parents.data(), frontier_parents.size() * sizeof(TreeId),
                       cudaMemcpyHostToDevice));
  for (auto level = 0; level < depth && frontier_size > 0; ++level) {
    cudaCheck(cudaMemset(next_size_, 0, sizeof(cuda_size_t)));
    cudaCheck(cudaMemset(result_size_, 0, sizeof(cuda_size_t)));
    int threads = 256, blocks = (frontier_size + threads - 1) / threads;
    cuda_size_t tree_size = trees.size();
    expand_tree<<<blocks, threads>>>(package_nodes_, version_nodes_, dependency_edges_, string_pool_, frontier_,
                                     frontier_trees_, frontier_size, next_, next_trees_, next_size_, result_,
                                     result_trees_, result_size_, visited_, mark_, depth, level, tree_size,
                                     arch, satisfy_architecture, satisfy_version, expand_alternative);
    cudaCheck(cudaGetLastError());
    cudaCheck(cudaDeviceSynchronize());
    cuda_size_t result_size;
    cudaCheck(cudaMemcpy(&result_size, result_size_, sizeof(cuda_size_t), cudaMemcpyDeviceToHost));
    if (result_size >= kDeviceVectorSize<DependencyId>) throw std::out_of_range("Reached max device vector size");
    std::vector<DependencyId> result_level(result_size);
    std::vector<TreeId> result_trees(result_size);
    cudaCheck(cudaMemcpy(result_level.data(), result_, result_size * sizeof(DependencyId), cudaMemcpyDeviceToHost));
    cudaCheck(cudaMemcpy(result_trees.data(), result_trees_, result_size * sizeof(TreeId),
                         cudaMemcpyDeviceToHost));

    std::unordered_map<TreeId, std::unordered_map<VersionId, std::vector<std::vector<TreeId>>>> groupsss;
    for (auto i : std::views::iota(0ull, result_size)) {
      const auto &dedge = graph_.dependency_edges_[result_level[i]];
      auto &groups = groupsss[result_trees[i]][dedge.from_version];
      const auto &pnode = graph_.package_nodes_[dedge.to_package];
      DependencyTree tree;
      tree.name = graph_.string_pool_[pnode.name];
      tree.type = graph_.dependency_types_[dedge.type];
      tree.version_constraint = graph_.string_pool_[dedge.version_constraint];
      tree.architecture_constraint = graph_.architecture_types_[dedge.architecture_constraint];
      auto tid = static_cast<TreeId>(trees.size());
      trees.emplace_back(std::move(tree));
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
      if (frontier_size >= kDeviceVectorSize<VersionId>) throw std::out_of_range("Reached max device vector size");
      std::swap(frontier_, next_);
      std::swap(frontier_trees_, next_trees_);
    }
  }
  auto build_tree = [&](const auto &self, TreeId tid) -> DependencyTree {
    auto tree = std::move(trees[tid].value);
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

__global__ void expand_flat(const CudaCache::PackageNode *package_nodes, const CudaCache::VersionNode *version_nodes,
                            const CudaCache::DependencyEdge *dependency_edges, const char *string_pool,
                            const VersionId *frontier, cuda_size_t frontier_size, VersionId *next,
                            cuda_size_t *next_size, DependencyId *result, cuda_size_t *result_size,
                            CudaCache::VisitedMark *visited, CudaCache::VisitedMark mark, cuda_size_t depth,
                            cuda_size_t level, ArchitectureType source, bool satisfy_architecture, bool satisfy_version,
                            bool expand_alternative) {
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
    if (pos < kDeviceVectorSize<DependencyId>) result[pos] = dedge.original;
    if (level + 1 < depth && (expand_alternative || dedge.group == 0)) {
      const auto &pnode = package_nodes[dedge.to_package];
      for (auto vid = pnode.version_begin; vid < pnode.version_begin + pnode.version_count; ++vid) {
        if (visited[vid] == mark) continue;
        const auto &next_vnode = version_nodes[vid];
        if (satisfy_architecture &&
          !xpg::satisfy_architecture(next_vnode.architecture, dedge.architecture_constraint, source)) { continue; }
        if (satisfy_version && !xpg::satisfy_version(
          {string_pool + next_vnode.version_id, next_vnode.version_length},
          {string_pool + dedge.version_constraint_id, dedge.version_constraint_length})) { continue; }
        auto old = visited[vid];
        if (atomicCAS(&visited[vid], old, mark) != mark) {
          pos = atomicAdd(next_size, 1);
          if (pos < kDeviceVectorSize<VersionId>) next[pos] = vid;
        }
      }
    }
  }
}

DependencyFlat CudaCache::query_dependency_flat(
  std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
  bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const {
  DependencyFlat result(depth);
  auto arch = graph_.architecture_types_.id(architecture).value_or(ArchitectureType::kNull);
  auto frontier = init_frontier(name, version, arch);
  if (frontier.empty()) return result;
  cuda_size_t frontier_size = frontier.size();
  cudaCheck(cudaMemcpy(frontier_, frontier.data(), frontier_size * sizeof(VersionId), cudaMemcpyHostToDevice));
  for (auto level : std::views::iota(0ull, depth)) {
    cudaCheck(cudaMemset(next_size_, 0, sizeof(cuda_size_t)));
    cudaCheck(cudaMemset(result_size_, 0, sizeof(cuda_size_t)));
    int threads = 256, blocks = (frontier_size + threads - 1) / threads;
    expand_flat<<<blocks, threads>>>(package_nodes_, version_nodes_, dependency_edges_, string_pool_, frontier_,
                                     frontier_size, next_, next_size_, result_, result_size_, visited_, mark_, depth,
                                     level, arch, satisfy_architecture, satisfy_version, expand_alternative);
    cudaCheck(cudaGetLastError());
    cudaCheck(cudaDeviceSynchronize());
    cuda_size_t result_size;
    cudaCheck(cudaMemcpy(&result_size, result_size_, sizeof(cuda_size_t), cudaMemcpyDeviceToHost));
    if (result_size >= kDeviceVectorSize<VersionId>) throw std::out_of_range("Reached max device vector size");
    std::vector<DependencyId> result_level(result_size);
    cudaCheck(cudaMemcpy(result_level.data(), result_, result_size * sizeof(DependencyId), cudaMemcpyDeviceToHost));

    std::unordered_map<VersionId, std::vector<std::vector<DependencyInfo>>> groupss;
    std::unordered_set<DependencyInfo> dvisited;
    std::unordered_map<VersionId, std::vector<std::unordered_set<DependencyInfo>>> visited_ginfoss;
    for (auto did : result_level) {
      const auto &dedge = graph_.dependency_edges_[did];
      auto &groups = groupss[dedge.from_version];
      auto &gvisited = visited_ginfoss[dedge.from_version];

      const auto &pnode = graph_.package_nodes_[dedge.to_package];
      DependencyInfo info;
      info.name = graph_.string_pool_[pnode.name];
      info.type = graph_.dependency_types_[dedge.type];
      info.version_constraint = graph_.string_pool_[dedge.version_constraint];
      info.architecture_constraint = graph_.architecture_types_[dedge.architecture_constraint];
      if (dedge.group > 0) {
        if (dedge.group > groups.size()) {
          groups.resize(dedge.group);
          gvisited.resize(dedge.group);
        }
        auto [it, succ] = gvisited[dedge.group - 1].emplace(info);
        if (succ) groups[dedge.group - 1].emplace_back(std::move(info));
      } else {
        auto [it, succ] = dvisited.emplace(info);
        if (succ) result[level].single_dependencies.emplace_back(std::move(info));
      }
    }
    for (auto &groups : groupss | std::views::values)
      for (auto &group : groups)
        if (!group.empty()) result[level].alternative_dependencies.emplace_back(std::move(group));
    if (level + 1 < depth) {
      cudaCheck(cudaMemcpy(&frontier_size, next_size_, sizeof(cuda_size_t), cudaMemcpyDeviceToHost));
      if (frontier_size >= kDeviceVectorSize<VersionId>) throw std::out_of_range("Reached max device vector size");
      std::swap(frontier_, next_);
      if (frontier_size == 0) break;
    }
  }
  if (++mark_ == 0) {
    cudaCheck(cudaMemset(visited_, 0, to_cache_version_id_.size() * sizeof(VisitedMark)));
    mark_ = 1;
  }
  return result;
}

} // namespace xpg
