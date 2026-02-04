#include "cache_graph.hpp"
#include <cuda_runtime.h>
#include <ranges>
#include "dependency_graph.hpp"
#include "storage_graph.hpp"

CacheGraph::CacheGraph(const StorageGraph &graph) noexcept
  : storage_graph_(graph), mark_(1), d_package_nodes_(nullptr), d_version_nodes_(nullptr), d_dependency_edges_(nullptr),
    d_frontier_(nullptr), d_next_(nullptr), d_next_size_(nullptr), d_dependency_ids_(nullptr),
    d_dependency_count_(nullptr), d_visited_(nullptr) {}

void CacheGraph::build_cache() {
  std::vector<PackageNode> pnodes;
  std::vector<VersionNode> vnodes;
  std::vector<DependencyEdge> dedges;
  to_cache_version_id_.resize(storage_graph_.version_count());

  for (const auto &pnode : storage_graph_.package_nodes_) {
    auto vbegin = static_cast<VersionId>(vnodes.size());
    auto vcount = static_cast<VersionCount>(0);

    for (auto vlid = pnode.version_list; vlid != storage_graph_.version_list_end;) {
      const auto &vlnode = storage_graph_.version_list_nodes_[vlid];
      for (auto vid = vlnode.version_begin; vid < vlnode.version_begin + vlnode.version_count; ++vid) {
        const auto &vnode = storage_graph_.version_nodes_[vid];
        auto cvid = static_cast<VersionId>(vnodes.size());
        to_cache_version_id_[vid] = cvid;
        vnodes.push_back({
          .architecture = vnode.architecture,
          .dependency_count = static_cast<DependencyCount>(vnode.dependency_count),
          .dependency_begin = static_cast<DependencyId>(dedges.size())
        });

        for (auto did = vnode.dependency_begin; did < vnode.dependency_begin + vnode.dependency_count; ++did) {
          const auto &dedge = storage_graph_.dependency_edges_[did];
          dedges.push_back({
            .original_id = did,
            .to_package = dedge.to_package,
            .architecture_constraint = dedge.architecture_constraint,
            .dependency_type = dedge.dependency_type,
            .group = dedge.group,
          });
        }
        ++vcount;
      }
      vlid = vlnode.next;
    }
    pnodes.push_back({
      .version_begin = vbegin,
      .version_count = vcount
    });
  }
  init_gpu(pnodes, vnodes, dedges);
  mark_ = 1;
}

void CacheGraph::init_gpu(const std::vector<PackageNode> &pnodes, const std::vector<VersionNode> &vnodes,
                          const std::vector<DependencyEdge> &dedges) {
  cudaMalloc(&d_package_nodes_, pnodes.size() * sizeof(PackageNode));
  cudaMalloc(&d_version_nodes_, vnodes.size() * sizeof(VersionNode));
  cudaMalloc(&d_dependency_edges_, dedges.size() * sizeof(DependencyEdge));
  cudaMalloc(&d_frontier_, kMaxDeviceVectorBytes);
  cudaMalloc(&d_next_, kMaxDeviceVectorBytes);
  cudaMalloc(&d_next_size_, sizeof(std::size_t));
  cudaMalloc(&d_dependency_ids_, kMaxDeviceVectorBytes);
  cudaMalloc(&d_dependency_count_, sizeof(std::size_t));
  cudaMalloc(&d_visited_, vnodes.size() * sizeof(VisitedMark));
  cudaMemcpy(d_package_nodes_, pnodes.data(), pnodes.size() * sizeof(PackageNode), cudaMemcpyHostToDevice);
  cudaMemcpy(d_version_nodes_, vnodes.data(), vnodes.size() * sizeof(VersionNode), cudaMemcpyHostToDevice);
  cudaMemcpy(d_dependency_edges_, dedges.data(), dedges.size() * sizeof(DependencyEdge), cudaMemcpyHostToDevice);
  cudaMemset(d_visited_, 0, vnodes.size() * sizeof(VisitedMark));
}

void CacheGraph::free_gpu() {
  if (d_package_nodes_) cudaFree(d_package_nodes_);
  if (d_version_nodes_) cudaFree(d_version_nodes_);
  if (d_dependency_edges_) cudaFree(d_dependency_edges_);
  if (d_frontier_) cudaFree(d_frontier_);
  if (d_next_) cudaFree(d_next_);
  if (d_next_size_) cudaFree(d_next_size_);
  if (d_dependency_ids_) cudaFree(d_dependency_ids_);
  if (d_dependency_count_) cudaFree(d_dependency_count_);
  if (d_visited_) cudaFree(d_visited_);
  d_package_nodes_ = nullptr;
  d_version_nodes_ = nullptr;
  d_dependency_edges_ = nullptr;
  d_frontier_ = nullptr;
  d_next_ = nullptr;
  d_next_size_ = nullptr;
  d_dependency_ids_ = nullptr;
  d_dependency_count_ = nullptr;
  d_visited_ = nullptr;
}

__global__ void expand_frontier_kernel(
  const CacheGraph::PackageNode *package_nodes, const CacheGraph::VersionNode *version_nodes,
  const CacheGraph::DependencyEdge *dependency_edges, const VersionId *frontier, cuda_size_t frontier_size,
  VersionId *next, cuda_size_t *next_size, DependencyId *dependency_ids, cuda_size_t *dependency_count,
  CacheGraph::VisitedMark *visited, CacheGraph::VisitedMark mark, bool first_level, bool has_next) {
  auto idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= frontier_size) return;
  if (first_level) {
    auto old = visited[frontier[idx]];
    atomicCAS(&visited[frontier[idx]], old, mark);
  }

  auto vid = frontier[idx];
  const auto &vnode = version_nodes[vid];
  for (auto did = vnode.dependency_begin; did < vnode.dependency_begin + vnode.dependency_count; ++did) {
    const auto &dedge = dependency_edges[did];
    auto pos = atomicAdd(dependency_count, 1);
    if (pos < kMaxDeviceVectorBytes) dependency_ids[pos] = dedge.original_id;

    if (has_next && dedge.dependency_type == 0 && dedge.group == 0) {
      const auto &tpnode = package_nodes[dedge.to_package];
      for (auto nvid = tpnode.version_begin; nvid < tpnode.version_begin + tpnode.version_count; ++nvid) {
        if (visited[nvid] == mark) continue;
        const auto &nvnode = version_nodes[nvid];
        bool match = false;
        if (dedge.architecture_constraint == 0)
          match = nvnode.architecture == vnode.architecture || nvnode.architecture == 2;
        else if (dedge.architecture_constraint == 1) match = true;
        else match = nvnode.architecture == dedge.architecture_constraint;
        if (match) {
          auto old = visited[nvid];
          if (atomicCAS(&visited[nvid], old, mark) != mark) {
            pos = atomicAdd(next_size, 1);
            if (pos < kMaxDeviceVectorBytes) next[pos] = nvid;
          }
        }
      }
    }
  }
}

DependencyLevel CacheGraph::expand_frontier(std::size_t &frontier_size, bool first_level, bool has_next) const {
  DependencyLevel result;
  cudaMemset(d_next_size_, 0, sizeof(std::size_t));
  cudaMemset(d_dependency_count_, 0, sizeof(std::size_t));
  int threads = 256, blocks = (frontier_size + threads - 1) / threads;
  expand_frontier_kernel<<<blocks, threads>>>(
    d_package_nodes_, d_version_nodes_, d_dependency_edges_, d_frontier_, frontier_size, d_next_, d_next_size_,
    d_dependency_ids_, d_dependency_count_, d_visited_, mark_, first_level, has_next);
  cudaDeviceSynchronize();

  std::size_t dcount;
  cudaMemcpy(&dcount, d_dependency_count_, sizeof(std::size_t), cudaMemcpyDeviceToHost);
  if (dcount >= kMaxDeviceVectorBytes) throw std::out_of_range("Reached max device vector size");
  std::vector<DependencyId> dids(dcount);
  cudaMemcpy(dids.data(), d_dependency_ids_, dcount * sizeof(DependencyId), cudaMemcpyDeviceToHost);
  std::unordered_map<VersionId, std::vector<DependencyGroup>> groups_by_ver;
  std::unordered_set<DependencyInfo> visited_dinfos;
  std::unordered_map<VersionId, std::vector<std::unordered_set<DependencyInfo>>> visited_ginfos_by_ver;

  for (auto did : dids) {
    const auto &dedge = storage_graph_.dependency_edges_[did];
    auto vid = dedge.from_version;
    auto &groups = groups_by_ver[vid];
    auto &visited_ginfos = visited_ginfos_by_ver[vid];
    auto info = storage_graph_.to_info(did);

    if (dedge.group > 0) {
      if (dedge.group > groups.size()) {
        groups.resize(dedge.group);
        visited_ginfos.resize(dedge.group);
      }
      auto [it, succ] = visited_ginfos[dedge.group - 1].emplace(info);
      if (succ) groups[dedge.group - 1].emplace_back(std::move(info));
    } else {
      auto [it, succ] = visited_dinfos.emplace(info);
      if (succ) result.direct_dependencies.emplace_back(std::move(info));
    }
  }
  for (auto &groups : groups_by_ver | std::views::values)
    for (auto &group : groups) if (!group.empty()) result.or_dependencies.emplace_back(std::move(group));

  if (has_next) {
    cudaMemcpy(&frontier_size, d_next_size_, sizeof(std::size_t), cudaMemcpyDeviceToHost);
    if (frontier_size >= kMaxDeviceVectorBytes) throw std::out_of_range("Reached max device vector size");
    std::swap(d_frontier_, d_next_);
  }
  return result;
}

DependencyResult CacheGraph::query_dependencies(std::vector<VersionId> &frontier, std::size_t depth) const {
  DependencyResult result(depth);
  if (frontier.empty()) return result;
  std::size_t frontier_size = frontier.size();
  for (auto &vid : frontier) vid = to_cache_version_id_[vid];
  cudaMemcpy(d_frontier_, frontier.data(), frontier_size * sizeof(VersionId), cudaMemcpyHostToDevice);
  for (auto level = 0; level < depth; ++level) {
    result[level] = expand_frontier(frontier_size, level == 0, level + 1 < depth);
    if (frontier_size == 0) break;
  }
  if (++mark_ == 0) {
    cudaMemset(d_visited_, 0, to_cache_version_id_.size() * sizeof(VisitedMark));
    mark_ = 1;
  }
  return result;
}
