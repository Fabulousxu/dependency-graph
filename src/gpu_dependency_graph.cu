#include "gpu_dependency_graph.hpp"
#include <chrono>
#include <cuda_runtime.h>
#include <format>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include "dependency_graph.hpp"

using GDG = GpuDependencyGraph;

void GDG::build(bool verbose) {
  std::size_t old_free_bytes, free_bytes, total_bytes;
  std::chrono::time_point<std::chrono::high_resolution_clock> start;
  if (verbose) {
    std::cout << "Building GPU dependency graph... ";
    cudaMemGetInfo(&old_free_bytes, &total_bytes);
    start = std::chrono::high_resolution_clock::now();
  }

  package_nodes_.resize(graph_.package_count());
  version_nodes_.resize(graph_.version_count());
  dependency_edges_.resize(graph_.dependency_count());
  to_cuda_version_id_.resize(graph_.version_count());
  VersionId curr_vid = 0;
  DependencyId curr_did = 0;
  for (auto pid = 0; pid < package_nodes_.size(); pid++) {
    const auto &pnode = graph_.get_package(pid);
    package_nodes_[pid].start_version_id = curr_vid;
    package_nodes_[pid].version_count = pnode.version_ids.size();
    for (auto vid : pnode.version_ids) {
      const auto &vnode = graph_.get_version(vid);
      version_nodes_[curr_vid].start_dependency_id = curr_did;
      version_nodes_[curr_vid].dependency_count = vnode.dependency_ids.size();
      version_nodes_[curr_vid].architecture = vnode.architecture;
      for (auto did : vnode.dependency_ids) {
        const auto &dep = graph_.get_dependency(did);
        dependency_edges_[curr_did].original_dependency_id = did;
        dependency_edges_[curr_did].from_version_id = curr_vid;
        dependency_edges_[curr_did].to_package_id = dep.to_package_id;
        dependency_edges_[curr_did].architecture_constraint = dep.architecture_constraint;
        dependency_edges_[curr_did].dependency_type = dep.dependency_type;
        dependency_edges_[curr_did++].group = dep.group;
      }
      to_cuda_version_id_[vid] = curr_vid++;
    }
  }
  to_device();

  if (verbose) {
    auto end = std::chrono::high_resolution_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    cudaMemGetInfo(&free_bytes, &total_bytes);
    std::cout << std::format("Done. ({} ms)\n", time.count());
    std::cout << std::format("Loaded dependency graph to GPU. GPU total memory: {} MB, GPU used memory: {} MB, GPU "
      "free memory: {} MB.\n",
      total_bytes / (1024 * 1024), (old_free_bytes - free_bytes) / (1024 * 1024),
      free_bytes / (1024 * 1024));
  }
}

void GDG::to_device() {
  frontier_.resize(MAX_DEVICE_VECTOR_SIZE);
  dependency_ids_.resize(MAX_DEVICE_VECTOR_SIZE);
  cudaMalloc(&d_package_nodes_, package_nodes_.size() * sizeof(PackageNode));
  cudaMalloc(&d_version_nodes_, version_nodes_.size() * sizeof(VersionNode));
  cudaMalloc(&d_dependency_edges_, dependency_edges_.size() * sizeof(DependencyEdge));
  cudaMalloc(&d_frontier_, MAX_DEVICE_VECTOR_SIZE * sizeof(VersionId));
  cudaMalloc(&d_next_, MAX_DEVICE_VECTOR_SIZE * sizeof(VersionId));
  cudaMalloc(&d_dependency_ids_, MAX_DEVICE_VECTOR_SIZE * sizeof(DependencyId));
  cudaMalloc(&d_next_size_, MAX_DEVICE_VECTOR_SIZE * sizeof(std::size_t));
  cudaMalloc(&d_dependency_count_, sizeof(std::size_t));
  cudaMalloc(&d_visited_, version_nodes_.size() * sizeof(VisitedMark));
  cudaMemcpy(d_package_nodes_, package_nodes_.data(), package_nodes_.size() * sizeof(PackageNode),
    cudaMemcpyHostToDevice);
  cudaMemcpy(d_version_nodes_, version_nodes_.data(), version_nodes_.size() * sizeof(VersionNode),
    cudaMemcpyHostToDevice);
  cudaMemcpy(d_dependency_edges_, dependency_edges_.data(), dependency_edges_.size() * sizeof(DependencyEdge),
    cudaMemcpyHostToDevice);
  cudaMemset(d_visited_, 0, version_nodes_.size() * sizeof(VisitedMark));
  mark_ = 1;
}

void GDG::free_device() {
  constexpr auto free = []<class T>(T *&ptr) {
    cudaFree(ptr);
    ptr = nullptr;
  };
  free(d_package_nodes_);
  free(d_version_nodes_);
  free(d_dependency_edges_);
  free(d_frontier_);
  free(d_next_);
  free(d_dependency_ids_);
  free(d_next_size_);
  free(d_dependency_count_);
  free(d_visited_);
}

__global__ void query_dependency_kernel(const GDG::PackageNode *package_nodes, const GDG::VersionNode *version_nodes,
  const GDG::DependencyEdge *dependency_edges, const VersionId *frontier, std::size_t frontier_size, VersionId *next,
  std::size_t *next_size, DependencyId *dependency_ids, std::size_t *dependency_count, VisitedMark *visited,
  VisitedMark mark, bool first_level, bool has_next) {
  auto idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= frontier_size) return;
  if (first_level) {
    auto old = visited[frontier[idx]];
    atomicCAS(&visited[frontier[idx]], old, mark);
  }

  const auto &vnode = version_nodes[frontier[idx]];
  for (auto did = vnode.start_dependency_id; did < vnode.start_dependency_id + vnode.dependency_count; did++) {
    const auto &dep = dependency_edges[did];
    if (auto pos = atomicAdd(dependency_count, 1); pos < MAX_DEVICE_VECTOR_SIZE) dependency_ids[pos] = did;
    if (has_next && dep.dependency_type == 0 && dep.group == 0) {
      const auto &dep_pnode = package_nodes[dep.to_package_id];
      for (auto next_vid = dep_pnode.start_version_id; next_vid < dep_pnode.start_version_id + dep_pnode.version_count;
           next_vid++) {
        const auto &next_vnode = version_nodes[next_vid];
        bool match = false;
        if (dep.architecture_constraint == 0)
          match = next_vnode.architecture == vnode.architecture || next_vnode.architecture == 2;
        else if (dep.architecture_constraint == 1) match = true;
        else match = next_vnode.architecture == dep.architecture_constraint;
        if (match)
          if (auto old = visited[next_vid]; atomicCAS(&visited[next_vid], old, mark) != mark)
            if (auto pos = atomicAdd(next_size, 1); pos < MAX_DEVICE_VECTOR_SIZE) next[pos] = next_vid;
      }
    }
  }
}

DependencyResult GDG::query_dependencies(const std::vector<VersionId> &vids, std::size_t depth) {
  DependencyResult result{depth};
  std::size_t frontier_size = vids.size(), dependency_count;
  frontier_.clear();
  for (auto vid : vids) frontier_.emplace_back(to_cuda_version_id_[vid]);
  cudaMemcpy(d_frontier_, frontier_.data(), frontier_size * sizeof(VersionId), cudaMemcpyHostToDevice);
  for (auto level = 0; level < depth; level++) {
    if (frontier_size == 0) break;
    cudaMemset(d_next_size_, 0, sizeof(std::size_t));
    cudaMemset(d_dependency_count_, 0, sizeof(std::size_t));
    int threads = 256, blocks = (frontier_size + threads - 1) / threads;
    query_dependency_kernel<<<blocks, threads>>>(d_package_nodes_, d_version_nodes_, d_dependency_edges_,
      d_frontier_, frontier_size, d_next_, d_next_size_, d_dependency_ids_, d_dependency_count_, d_visited_,
      mark_, level == 0, level + 1 < depth);
    cudaDeviceSynchronize();

    cudaMemcpy(&dependency_count, d_dependency_count_, sizeof(std::size_t), cudaMemcpyDeviceToHost);
    if (dependency_count >= MAX_DEVICE_VECTOR_SIZE)
      std::cout << std::format("Warning: reached max device vector size when querying dependencies at depth {}.\n",
        level + 1);
    dependency_ids_.resize(dependency_count);
    cudaMemcpy(dependency_ids_.data(), d_dependency_ids_, dependency_count * sizeof(DependencyId),
      cudaMemcpyDeviceToHost);
    std::unordered_map<VersionId, std::vector<DependencyGroup>> grps_by_version;
    std::unordered_set<DependencyItem> visited_direct_items;
    std::unordered_map<VersionId, std::vector<std::unordered_set<DependencyItem>>> visited_grp_items_by_version;
    for (auto did : dependency_ids_) {
      const auto &dedge = dependency_edges_[did];
      auto item = graph_.to_item(graph_.get_dependency(dedge.original_dependency_id));
      if (dedge.group != 0) {
        auto vid = dedge.from_version_id;
        auto &curr_grps = grps_by_version[vid];
        auto &curr_visited_grp_items = visited_grp_items_by_version[vid];
        if (dedge.group > curr_grps.size()) {
          curr_grps.resize(dedge.group);
          curr_visited_grp_items.resize(dedge.group);
        }
        if (curr_visited_grp_items[dedge.group - 1].emplace(item).second)
          curr_grps[dedge.group - 1].emplace_back(std::move(item));
      } else if (visited_direct_items.emplace(item).second)
        result[level].direct_dependencies.emplace_back(std::move(item));
    }
    for (auto &&curr_grps : grps_by_version | std::views::values)
      for (auto &&grp : curr_grps) if (!grp.empty()) result[level].or_dependencies.emplace_back(std::move(grp));

    if (level + 1 < depth) {
      cudaMemcpy(&frontier_size, d_next_size_, sizeof(std::size_t), cudaMemcpyDeviceToHost);
      if (frontier_size >= MAX_DEVICE_VECTOR_SIZE)
        std::cout << std::format("Warning: reached max device vector size when querying dependencies at depth {}.\n",
          level + 1);
      std::swap(d_frontier_, d_next_);
    }
  }
  if (++mark_ == 0) {
    cudaMemset(d_visited_, 0, version_nodes_.size() * sizeof(VisitedMark));
    mark_ = 1;
  }
  return result;
}
