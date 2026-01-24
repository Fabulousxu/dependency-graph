#include "dependency_graph.hpp"
#include <cuda_runtime.h>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "util.hpp"

DependencyGraph::DependencyGraph(std::size_t memory_limit, std::size_t chunk_bytes) noexcept
  : disk_graph_(chunk_bytes), memory_limit_(memory_limit) {}

DependencyGraph::DependencyGraph(const std::filesystem::path &directory_path, open_mode mode, std::size_t memory_limit,
                                 std::size_t chunk_bytes) noexcept
  : DependencyGraph(memory_limit, chunk_bytes) {
  open(directory_path, mode);
}

open_code DependencyGraph::open(const std::filesystem::path &directory_path, open_mode mode) noexcept {
  return disk_graph_.open(
    directory_path, mode, {"native", "any", "all"},
    {"Depends", "Pre-Depends", "Recommends", "Suggests", "Breaks", "Conflicts", "Provides", "Replaces", "Enhances"});
}

void DependencyGraph::close() {
  flush_buffer();
  free_gpu();
  disk_graph_.close();
}

void DependencyGraph::flush_buffer() {
  disk_graph_.ingest(buf_graph_);
  buf_graph_.clear();
}

bool DependencyGraph::flush_buffer_if_needed() {
  auto needed = estimated_memory_usage() >= memory_limit_;
  if (needed) flush_buffer();
  return needed;
}

std::size_t DependencyGraph::estimated_memory_usage() const noexcept {
  return sizeof(DependencyGraph) + buf_graph_.estimated_memory_usage() - sizeof(BufferGraph);
}

std::pair<PackageId, bool> DependencyGraph::create_package(std::string_view name) {
  return buf_graph_.create_package(name);
}

std::pair<VersionId, bool> DependencyGraph::create_version(PackageId pid, std::string_view version,
                                                           ArchitectureType arch) {
  return buf_graph_.create_version(pid, version, arch);
}

std::pair<DependencyId, bool> DependencyGraph::create_dependency(VersionId from_vid, PackageId to_pid,
                                                                 std::string_view vcons, ArchitectureType acons,
                                                                 DependencyType dtype, GroupId gid) {
  return buf_graph_.create_dependency(from_vid, to_pid, vcons, acons, dtype, gid);
}

DependencyResult DependencyGraph::query_dependencies(std::string_view name, std::string_view version,
                                                     std::string_view arch, std::size_t depth, bool use_gpu) const {
  std::vector<VersionId> frontier;
  auto it = disk_graph_.name_to_package_id_.find(name);
  if (it != disk_graph_.name_to_package_id_.end()) {
    const auto &pnode = disk_graph_.package_nodes_[it->second];
    for (auto vlid = pnode.version_list_id; vlid != DiskGraph::kVersionListEndId;) {
      const auto &vlist = disk_graph_.version_lists_[vlid];
      for (auto vid = vlist.version_id_begin; vid < vlist.version_id_begin + vlist.version_count; ++vid) {
        const auto &vnode = disk_graph_.version_nodes_[vid];
        auto vstr = disk_graph_.string_pool_.get(vnode.version_offset, vnode.version_length);
        if (!version.empty() && vstr != version) continue;
        if (!arch.empty() && architectures()[vnode.architecture] != arch) continue;
        frontier.emplace_back(vid);
      }
      vlid = vlist.next_version_list_id;
    }
  }
  return use_gpu ? query_dependencies_on_gpu(frontier, depth) : query_dependencies_on_disk(frontier, depth);
}

DependencyResult DependencyGraph::query_dependencies_on_buffer(std::string_view name, std::string_view version,
                                                               std::string_view arch, std::size_t depth) const {
  DependencyResult result(depth);
  std::vector<VersionId> frontier;
  auto opt = buf_graph_.get_package(name);
  if (!opt.has_value()) return result;
  for (auto vid : opt->get().version_ids) {
    const auto &vnode = buf_graph_.get_version(vid);
    if (!version.empty() && vnode.version != version) continue;
    if (!arch.empty() && architectures()[vnode.architecture] != arch) continue;
    frontier.emplace_back(vid);
  }
  if (frontier.empty()) return result;
  std::unordered_set visited_vids(frontier.begin(), frontier.end());

  for (auto level = 0; level < depth; ++level) {
    std::unordered_set<DependencyItem> visited_direct_items;
    std::vector<VersionId> next;

    for (auto vid : frontier) {
      const auto &vnode = buf_graph_.get_version(vid);
      std::vector<DependencyGroup> vgroups;
      std::vector<std::unordered_set<DependencyItem>> visited_group_items;

      for (auto did : vnode.dependency_ids) {
        const auto &dedge = buf_graph_.get_dependency(did);
        const auto &tpnode = buf_graph_.get_package(dedge.to_package_id);
        DependencyItem item{
          .package_name = tpnode.name,
          .dependency_type = dependency_types()[dedge.dependency_type],
          .version_constraint = dedge.version_constraint,
          .architecture_constraint = architectures()[dedge.architecture_constraint]
        };

        if (dedge.group > 0) {
          if (vgroups.size() < dedge.group) {
            vgroups.resize(dedge.group);
            visited_group_items.resize(dedge.group);
          }
          if (visited_group_items[dedge.group - 1].emplace(item).second)
            vgroups[dedge.group - 1].emplace_back(std::move(item));
        } else if (visited_direct_items.emplace(item).second)
          result[level].direct_dependencies.emplace_back(std::move(item));

        if (level + 1 < depth && dependency_types()[dedge.dependency_type] == "Depends" && dedge.group == 0)
          for (auto nvid : tpnode.version_ids) {
            if (visited_vids.contains(nvid)) continue;
            const auto &nvnode = buf_graph_.get_version(nvid);

            bool match = false;
            if (architectures()[dedge.architecture_constraint] == "native")
              match = nvnode.architecture == vnode.architecture || architectures()[nvnode.architecture] == "all";
            else if (architectures()[dedge.architecture_constraint] == "any") match = true;
            else match = nvnode.architecture == dedge.architecture_constraint;

            if (match) {
              next.emplace_back(nvid);
              visited_vids.emplace(nvid);
            }
          }
      }
      for (auto &group : vgroups) if (!group.empty()) result[level].or_dependencies.emplace_back(std::move(group));
    }
    frontier = std::move(next);
    if (frontier.empty()) break;
  }
  return result;
}

DependencyResult DependencyGraph::query_dependencies_on_disk(std::vector<VersionId> &frontier,
                                                             std::size_t depth) const {
  DependencyResult result(depth);
  if (frontier.empty()) return result;
  std::unordered_set visited_vids(frontier.begin(), frontier.end());

  for (auto level = 0; level < depth; ++level) {
    std::unordered_set<DependencyItem> visited_direct_items;
    std::vector<VersionId> next;

    for (auto vid : frontier) {
      const auto &vnode = disk_graph_.version_nodes_[vid];
      std::vector<DependencyGroup> vgroups;
      std::vector<std::unordered_set<DependencyItem>> visited_group_items;

      for (auto did = vnode.dependency_id_begin; did < vnode.dependency_id_begin + vnode.dependency_count; ++did) {
        const auto &dedge = disk_graph_.dependency_edges_[did];
        const auto &tpnode = disk_graph_.package_nodes_[dedge.to_package_id];
        DependencyItem item{
          .package_name = disk_graph_.string_pool_.get(tpnode.name_offset, tpnode.name_length),
          .dependency_type = dependency_types()[dedge.dependency_type],
          .version_constraint = disk_graph_.string_pool_.get(
            dedge.version_constraint_offset, dedge.version_constraint_length),
          .architecture_constraint = architectures()[dedge.architecture_constraint]
        };

        if (dedge.group > 0) {
          if (vgroups.size() < dedge.group) {
            vgroups.resize(dedge.group);
            visited_group_items.resize(dedge.group);
          }
          if (visited_group_items[dedge.group - 1].emplace(item).second)
            vgroups[dedge.group - 1].emplace_back(std::move(item));
        } else if (visited_direct_items.emplace(item).second)
          result[level].direct_dependencies.emplace_back(std::move(item));

        if (level + 1 < depth && dependency_types()[dedge.dependency_type] == "Depends" && dedge.group == 0)
          for (auto vlid = tpnode.version_list_id; vlid != DiskGraph::kVersionListEndId;) {
            const auto &vlist = disk_graph_.version_lists_[vlid];
            for (auto nvid = vlist.version_id_begin; nvid < vlist.version_id_begin + vlist.version_count; ++nvid) {
              if (visited_vids.contains(nvid)) continue;
              const auto &nvnode = disk_graph_.version_nodes_[nvid];

              bool match = false;
              if (architectures()[dedge.architecture_constraint] == "native")
                match = nvnode.architecture == vnode.architecture || architectures()[nvnode.architecture] == "all";
              else if (architectures()[dedge.architecture_constraint] == "any") match = true;
              else match = nvnode.architecture == dedge.architecture_constraint;

              if (match) {
                next.emplace_back(nvid);
                visited_vids.emplace(nvid);
              }
            }
            vlid = vlist.next_version_list_id;
          }
      }
      for (auto &group : vgroups) if (!group.empty()) result[level].or_dependencies.emplace_back(std::move(group));
    }
    frontier = std::move(next);
    if (frontier.empty()) break;
  }
  return result;
}

__global__ void query_dependency_kernel(const GpuGraph::PackageNode *package_nodes,
                                        const GpuGraph::VersionNode *version_nodes,
                                        const GpuGraph::DependencyEdge *dependency_edges,
                                        const VersionId *frontier, std::size_t frontier_size,
                                        VersionId *next, std::size_t *next_size,
                                        DependencyId *dependency_ids, std::size_t *dependency_count,
                                        GpuGraph::VisitedMarkType *visited, GpuGraph::VisitedMarkType mark,
                                        bool first_level, bool has_next) {
  auto idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= frontier_size) return;
  if (first_level) {
    auto old = visited[frontier[idx]];
    atomicCAS(&visited[frontier[idx]], old, mark);
  }

  const auto &vnode = version_nodes[frontier[idx]];
  for (auto did = vnode.dependency_id_begin; did < vnode.dependency_id_begin + vnode.dependency_count; ++did) {
    const auto &dedge = dependency_edges[did];
    auto pos = atomicAdd(dependency_count, 1);
    if (pos < kDefaultMaxDeviceVectorBytes) dependency_ids[pos] = dedge.original_dependency_id;

    if (has_next && dedge.dependency_type == 0 && dedge.group == 0) {
      const auto &tpnode = package_nodes[dedge.to_package_id];
      for (auto nvid = tpnode.version_id_begin; nvid < tpnode.version_id_begin + tpnode.version_count; ++nvid) {
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
            if (pos < kDefaultMaxDeviceVectorBytes) next[pos] = nvid;
          }
        }
      }
    }
  }
}

DependencyResult DependencyGraph::query_dependencies_on_gpu(std::vector<VersionId> &frontier, std::size_t depth) const {
  DependencyResult result(depth);
  if (frontier.empty()) return result;
  std::size_t frontier_size = frontier.size(), dependency_count;
  std::vector<DependencyId> dependency_ids_;
  for (auto &vid : frontier) vid = gpu_graph_.to_gpu_version_id_[vid];
  cudaMemcpy(gpu_graph_.d_frontier_, frontier.data(), frontier_size * sizeof(VersionId), cudaMemcpyHostToDevice);

  for (auto level = 0; level < depth; ++level) {
    cudaMemset(gpu_graph_.d_next_size_, 0, sizeof(std::size_t));
    cudaMemset(gpu_graph_.d_dependency_count_, 0, sizeof(std::size_t));
    int threads = 256, blocks = (frontier_size + threads - 1) / threads;
    query_dependency_kernel<<<blocks, threads>>>(
      gpu_graph_.d_package_nodes_, gpu_graph_.d_version_nodes_, gpu_graph_.d_dependency_edges_,
      gpu_graph_.d_frontier_, frontier_size, gpu_graph_.d_next_, gpu_graph_.d_next_size_,
      gpu_graph_.d_dependency_ids_, gpu_graph_.d_dependency_count_, gpu_graph_.d_visited_, gpu_graph_.mark_,
      level == 0, level + 1 < depth);
    cudaDeviceSynchronize();

    cudaMemcpy(&dependency_count, gpu_graph_.d_dependency_count_, sizeof(std::size_t), cudaMemcpyDeviceToHost);
    if (dependency_count >= kDefaultMaxDeviceVectorBytes) throw std::out_of_range("Reached max device vector size");
    dependency_ids_.resize(dependency_count);
    cudaMemcpy(dependency_ids_.data(), gpu_graph_.d_dependency_ids_, dependency_count * sizeof(DependencyId),
               cudaMemcpyDeviceToHost);

    std::unordered_map<VersionId, std::vector<DependencyGroup>> groups_by_version;
    std::unordered_set<DependencyItem> visited_direct_items;
    std::unordered_map<VersionId, std::vector<std::unordered_set<DependencyItem>>> visited_group_items_by_version;
    for (auto did : dependency_ids_) {
      const auto &dedge = disk_graph_.dependency_edges_[did];
      const auto &tpnode = disk_graph_.package_nodes_[dedge.to_package_id];
      DependencyItem item{
        .package_name = disk_graph_.string_pool_.get(tpnode.name_offset, tpnode.name_length),
        .dependency_type = dependency_types()[dedge.dependency_type],
        .version_constraint = disk_graph_.string_pool_.get(
          dedge.version_constraint_offset, dedge.version_constraint_length),
        .architecture_constraint = architectures()[dedge.architecture_constraint]
      };

      if (dedge.group > 0) {
        auto vid = dedge.from_version_id;
        auto &vgroups = groups_by_version[vid];
        auto &visited_group_items = visited_group_items_by_version[vid];
        if (dedge.group > vgroups.size()) {
          vgroups.resize(dedge.group);
          visited_group_items.resize(dedge.group);
        }
        if (visited_group_items[dedge.group - 1].emplace(item).second)
          vgroups[dedge.group - 1].emplace_back(std::move(item));
      } else if (visited_direct_items.emplace(item).second)
        result[level].direct_dependencies.emplace_back(std::move(item));
    }

    for (auto &vgroups : groups_by_version | std::views::values)
      for (auto &group : vgroups) if (!group.empty()) result[level].or_dependencies.emplace_back(std::move(group));

    if (level + 1 < depth) {
      cudaMemcpy(&frontier_size, gpu_graph_.d_next_size_, sizeof(std::size_t), cudaMemcpyDeviceToHost);
      if (frontier_size >= kDefaultMaxDeviceVectorBytes) throw std::out_of_range("Reached max device vector size");
      std::swap(gpu_graph_.d_frontier_, gpu_graph_.d_next_);
    }
    if (frontier_size == 0) break;
  }
  if (++gpu_graph_.mark_ == 0) {
    cudaMemset(gpu_graph_.d_visited_, 0, gpu_graph_.to_gpu_version_id_.size() * sizeof(GpuGraph::VisitedMarkType));
    gpu_graph_.mark_ = 1;
  }
  return result;
}
