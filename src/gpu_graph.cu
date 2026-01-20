#include "gpu_graph.hpp"
#include <cuda_runtime.h>
#include <vector>
#include "dependency_graph.hpp"

GpuGraph::GpuGraph()
  : mark_{1}, d_package_nodes_{nullptr}, d_version_nodes_{nullptr}, d_dependency_edges_{nullptr}, d_frontier_{nullptr},
    d_next_{nullptr}, d_next_size_{nullptr}, d_dependency_ids_{nullptr}, d_dependency_count_{nullptr},
    d_visited_{nullptr} {}

void GpuGraph::build(const DependencyGraph &graph) {
  std::vector<PackageNode> pnodes;
  std::vector<VersionNode> vnodes;
  std::vector<DependencyEdge> dedges;
  to_gpu_version_id_.resize(graph.version_count());
  for (auto pid = 0; pid < graph.package_count(); pid++) {
    auto pview = graph.get_package(pid);
    auto vviews = pview.versions();
    auto &pnode = pnodes.emplace_back();
    pnode.version_id_begin = vnodes.size();
    pnode.version_count = vviews.size();

    for (const auto &vview : vviews) {
      auto dviews = vview.dependencies();
      to_gpu_version_id_[vview.id] = vnodes.size();
      auto &vnode = vnodes.emplace_back();
      vnode.architecture = graph.architectures().id(vview.architecture).value();
      vnode.dependency_id_begin = dedges.size();
      vnode.dependency_count = dviews.size();

      for (const auto &dview : dviews) {
        auto &dedge = dedges.emplace_back();
        dedge.original_dependency_id = dview.id;
        dedge.to_package_id = dview.to_package().id;
        dedge.architecture_constraint = graph.architectures().id(dview.architecture_constraint).value();
        dedge.dependency_type = graph.dependency_types().id(dview.dependency_type).value();
        dedge.group = dview.group;
      }
    }
  }
  cudaMalloc(&d_package_nodes_, pnodes.size() * sizeof(PackageNode));
  cudaMalloc(&d_version_nodes_, vnodes.size() * sizeof(VersionNode));
  cudaMalloc(&d_dependency_edges_, dedges.size() * sizeof(DependencyEdge));
  cudaMalloc(&d_frontier_, kMaxDeviceVectorSize * sizeof(VersionId));
  cudaMalloc(&d_next_, kMaxDeviceVectorSize * sizeof(VersionId));
  cudaMalloc(&d_next_size_, sizeof(std::size_t));
  cudaMalloc(&d_dependency_ids_, kMaxDeviceVectorSize * sizeof(DependencyId));
  cudaMalloc(&d_dependency_count_, sizeof(std::size_t));
  cudaMalloc(&d_visited_, vnodes.size() * sizeof(VisitedMarkType));
  cudaMemcpy(d_package_nodes_, pnodes.data(), pnodes.size() * sizeof(PackageNode), cudaMemcpyHostToDevice);
  cudaMemcpy(d_version_nodes_, vnodes.data(), vnodes.size() * sizeof(VersionNode), cudaMemcpyHostToDevice);
  cudaMemcpy(d_dependency_edges_, dedges.data(), dedges.size() * sizeof(DependencyEdge), cudaMemcpyHostToDevice);
  cudaMemset(d_visited_, 0, vnodes.size() * sizeof(VisitedMarkType));
  mark_ = 1;
}

void GpuGraph::free_device() {
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
