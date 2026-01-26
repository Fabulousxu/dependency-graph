# Dependency Graph System

This project implements a high-performance software package dependency graph system for storing and querying dependency relationships among large-scale software packages.

The system is based on a persistent, memory-mapped graph representation and supports optional CUDA-based GPU acceleration for efficient depth-limited dependency traversal queries.

At its current stage, the project mainly focuses on dependency query performance and system design validation, and can be used for research, benchmarking, or as a foundational component for higher-level systems.

---

## Features

- **Dependency Graph Storage**
    - Persistent, memory-mapped representation of packages, versions, and dependency relationships
    - Disk-backed storage design suitable for memory-constrained environments
    - Compact adjacency-style data layout optimized for dependency traversal

- **Efficient Dependency Queries**
    - Supports BFS-based, depth-limited dependency traversal
    - CPU and GPU execution modes are both available
    - Optimized for high-frequency query scenarios

- **GPU Acceleration**
    - Optional GPU-accelerated dependency traversal
    - Explicit synchronization of the full dependency graph to GPU memory
    - Parallel dependency traversal leveraging GPU computing capabilities

- **Modular Design**
    - Core functionality encapsulated in the `DependencyGraph` class
    - Can be integrated as a library into other projects

---

## Performance Evaluation

The project includes multiple performance evaluation results located in the
`results/` directory. These results can be reproduced using the provided benchmarking scripts in the
`benchmarks/` directory.

The benchmarks evaluate, among others:

- Query latency under different dependency depths
- Performance comparison between CPU and GPU execution
- Average latency and distribution over repeated queries

These results are primarily used to:

- Validate the effectiveness of the system design
- Compare with other dependency graph implementations or graph processing systems

> Note:  
> The provided results reflect relative performance under experimental environments and are not intended to represent absolute production-level performance.

---

## Build and Installation

### Environment Requirements

- **Operating System**: Windows x64
- **Toolchain**: MSVC, CUDA Toolkit, Windows SDK
- **Build System**: CMake, Ninja

### Build Steps

```powershell
git clone https://github.com/Fabulousxu/dependency-graph.git
mkdir build -Force
cd build
cmake .. -G Ninja
ninja
```

---

## Usage

The project is primarily designed as a C++ library. Users are expected to integrate it into their own applications and control data loading and query execution explicitly.

### Linking with CMake

```cmake
target_link_libraries(YOUR_TARGET_NAME PRIVATE libdepgraph)
```

### Example

```c++
#include "DependencyGraph.hpp"
#include "PackageLoader.hpp"
#include <nlohmann/json.hpp>

int main() {
    DependencyGraph graph("./data", kLoadOrCreate);
    PackageLoader loader(graph);
    loader.load_from_file("Debian-bookworm-amd64-Packages");
    graph.flush_to_disk();
    graph.sync_to_gpu();
    
    nlohmann::json result = graph.query_dependencies("adduser", "3.134", "all", 3, true);
    return 0;
}
```

---

## Console Application

A simple console application is also provided for interactive dependency querying.

After building, you can run the `console` executable directly from the build directory.

---

## Project Status

- Current version: v1.1 (Persistent Storage Release)
- This release introduces persistent, memory-mapped graph storage
- GPU acceleration is supported via full-graph synchronization
- APIs may change in future versions as GPU memory management is further refined
- The project is under active development and optimization