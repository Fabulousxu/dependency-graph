#include "gpu_graph.hpp"
#include <cuda_runtime.h>
#include <vector>
#include "dependency_graph.hpp"
#include "disk_graph.hpp"

GpuGraph::GpuGraph() noexcept
  : mark_(1), d_package_nodes_(nullptr), d_version_nodes_(nullptr), d_dependency_edges_(nullptr),
    d_frontier_(nullptr), d_next_(nullptr), d_next_size_(nullptr),
    d_dependency_ids_(nullptr), d_dependency_count_(nullptr), d_visited_(nullptr) {}

void GpuGraph::build(const DiskGraph &dgraph, std::size_t max_device_vector_bytes) {
  std::vector<PackageNode> pnodes;
  std::vector<VersionNode> vnodes;
  std::vector<DependencyEdge> dedges;
  to_gpu_version_id_.resize(dgraph.version_count());

  for (PackageId pid = 0; pid < dgraph.package_count(); ++pid) {
    auto pview = dgraph.get_package(pid);
    auto vviews = pview.versions();
    pnodes.push_back({
      .version_id_begin = static_cast<VersionId>(vnodes.size()),
      .version_count = static_cast<VersionCountType>(vviews.size())
    });

    for (const auto &vview : vviews) {
      to_gpu_version_id_[vview.id] = vnodes.size();
      auto dviews = vview.dependencies();
      vnodes.push_back({
        .dependency_id_begin = static_cast<DependencyId>(dedges.size()),
        .dependency_count = static_cast<DependencyCountType>(dviews.size()),
        .architecture = *dgraph.architectures()[vview.architecture]
      });

      for (const auto &dview : dviews)
        dedges.push_back({
          .original_dependency_id = dview.id,
          .to_package_id = dview.to_package().id,
          .architecture_constraint = *dgraph.architectures()[dview.architecture_constraint],
          .dependency_type = *dgraph.dependency_types()[dview.dependency_type],
          .group = dview.group,
        });
    }
  }

  cudaMalloc(&d_package_nodes_, pnodes.size() * sizeof(PackageNode));
  cudaMalloc(&d_version_nodes_, vnodes.size() * sizeof(VersionNode));
  cudaMalloc(&d_dependency_edges_, dedges.size() * sizeof(DependencyEdge));
  cudaMalloc(&d_frontier_, max_device_vector_bytes);
  cudaMalloc(&d_next_, max_device_vector_bytes);
  cudaMalloc(&d_next_size_, sizeof(std::size_t));
  cudaMalloc(&d_dependency_ids_, max_device_vector_bytes);
  cudaMalloc(&d_dependency_count_, sizeof(std::size_t));
  cudaMalloc(&d_visited_, vnodes.size() * sizeof(VisitedMarkType));
  cudaMemcpy(d_package_nodes_, pnodes.data(), pnodes.size() * sizeof(PackageNode), cudaMemcpyHostToDevice);
  cudaMemcpy(d_version_nodes_, vnodes.data(), vnodes.size() * sizeof(VersionNode), cudaMemcpyHostToDevice);
  cudaMemcpy(d_dependency_edges_, dedges.data(), dedges.size() * sizeof(DependencyEdge), cudaMemcpyHostToDevice);
  cudaMemset(d_visited_, 0, vnodes.size() * sizeof(VisitedMarkType));
  mark_ = 1;
}

void GpuGraph::free() {
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
