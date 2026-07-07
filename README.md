# XPGraph: Fast Package Dependency Query System

XPGraph is a C++ package dependency graph library for loading Linux package metadata and querying package dependencies efficiently. It supports DEB `Packages` and RPM `primary.xml` metadata, stores the graph in a persistent disk-backed format, and can optionally use CUDA to accelerate dependency traversal.

The current version focuses on large-scale multi-ecosystem repository dependency analysis. It is intended for experiments, benchmarks, and integration into tools that need fast package, version, and dependency queries.

---

## Features

- **Persistent graph storage**
  - Stores packages, versions, dependency edges, architectures, and dependency types in a reusable on-disk graph.
  - Uses memory-mapped storage to access graph data directly from mapped files, reducing explicit file I/O and improving read performance on large graphs.
  - Supports create, load, and load-or-create open modes.

- **Repository metadata loading**
  - Loads local DEB `Packages` files.
  - Loads local RPM `primary.xml` files.
  - Loads multiple repositories from one JSON repository configuration.
  - Provides a Python downloader for preparing the local repository metadata cache.

- **Dependency queries**
  - Lists packages by architecture and optional name prefix.
  - Lists available versions for one package.
  - Queries dependencies with configurable depth.
  - Supports tree and flat query result formats.
  - Supports architecture/version filtering and alternative dependency expansion.

- **CUDA acceleration**
  - Builds an explicit GPU cache from the persisted graph.
  - Can run dependency traversal on CPU or GPU.
  - CPU remains available for environments where GPU acceleration is not needed.

- **Library and CLI usage**
  - Provides the `libxpgraph` static library for C++ applications.
  - Provides an interactive `console` executable for loading and querying data.
  - Provides the `pyxpgraph` Python extension module for query-side scripting.
  - Provides benchmark, correctness test, and profiling executables.

---

## Requirements

The project is built with CMake and Ninja. The versions below are the versions used during development:

- GCC 14.3
- CUDA 12.9
- Python 3.10
- CMake 4.3
- Ninja 1.13

---

## Build

Clone the repository and build the release preset:

```sh
git clone https://github.com/fabulousxu/XPGraph.git
cd XPGraph

cmake --preset Release
cmake --build build/release -j
```

The build outputs are placed under `build/release`:

```text
build/release/bin/console
build/release/bin/query_dependencies_benchmark
build/release/bin/query_dependencies_correctness_test
build/release/bin/query_dependencies_profile
build/release/lib/libxpgraph.a
build/release/python/pyxpgraph.so
```

For profiling builds with debug symbols and frame pointers:

```sh
cmake --preset RelWithDebInfo
cmake --build build/relwithdebinfo -j
```

---

## Prepare Repository Metadata

XPGraph loads package metadata from local files. For large experiments, the recommended workflow is:

1. Describe the repositories in a JSON configuration file.
2. Download package indexes into a local cache directory.
3. Load the cached indexes into an XPGraph data directory.

Example repository configuration:

```json
{
  "debian": {
    "enabled": true,
    "type": "DEB",
    "urls": [
      "https://deb.debian.org/debian",
      "https://archive.debian.org/debian",
      "https://mirror.sjtu.edu.cn/debian"
    ],
    "distributions": ["forky", "trixie"],
    "architectures": ["all", "amd64", "arm64"]
  },
  "fedora": {
    "enabled": true,
    "type": "RPM",
    "urls": [
      "https://download.fedoraproject.org/pub/fedora/linux/releases",
      "https://ftp.sjtu.edu.cn/fedora/linux/releases"
    ],
    "distributions": ["42", "43", "44"],
    "architectures": ["aarch64", "source", "x86_64"]
  }
}
```

Download the configured metadata:

```sh
python scripts/download_repository.py \
  --repository-config data/repos/repoconfig.json \
  --cache-directory data/repos
```

The downloader stores DEB indexes as:

```text
data/repos/<repo>/<distribution>/<architecture>
```

and RPM indexes as:

```text
data/repos/<repo>/<distribution>/<architecture>.xml
```

You can also use `--scan-all` to scan repository URLs and download all discoverable distributions and architectures instead of only the configured targets.

---

## Console Application

The `console` executable provides an interactive interface for loading package metadata and querying the graph.

Open an existing graph or create it if it does not exist:

```sh
build/release/bin/console \
  --open-mode load-or-create \
  --data-directory data/xpg/example
```

Create a graph and load repositories from a prepared metadata cache:

```sh
build/release/bin/console \
  --open-mode create \
  --data-directory data/xpg/example \
  --repository-config data/repos/repoconfig.json \
  --cache-directory data/repos
```

`--open-mode` accepts:

- `load`: open an existing graph.
- `create`: create a new graph directory.
- `load-or-create`: open the graph if it exists, otherwise create it.

After startup, the console supports these commands:

- `l`: load one DEB `Packages` file or RPM XML file.
- `r`: load repositories from a repository configuration file.
- `c`: compact the persisted graph storage.
- `f`: flush buffered package data into persistent storage.
- `g`: build the GPU cache.
- `p`: query packages.
- `v`: query versions of a package.
- `d`: query dependencies.
- `q`: quit.

When `--repository-config` is provided at startup, the console loads the configured repositories, flushes the in-memory buffer to disk, and builds the GPU cache before entering interactive mode.

---

## Use as a C++ Library

XPGraph is designed to be embedded in C++ applications. Link your executable
with `libxpgraph`:

```cmake
target_link_libraries(example PRIVATE libxpgraph)
```

The typical library workflow is:

1. Open or create an `xpg::XPGraph` data directory.
2. Create an `xpg::PackageLoader` bound to that graph.
3. Load package metadata from local files or a repository configuration.
4. Flush buffered data into persistent graph storage.
5. Optionally build the GPU cache.
6. Run package, version, or dependency queries.

Example:

```cpp
#include "xpgraph.hpp"
#include "package_loader.hpp"
#include "json_serialization.hpp"

int main() {
  xpg::XPGraph graph("data/xpg/example", xpg::open_mode::kLoadOrCreate);

  // PackageLoader parses repository metadata and writes packages
  // into XPGraph's in-memory buffer first.
  xpg::PackageLoader loader(graph);

  // Load one Debian Packages index.
  loader.load_packages("data/repos/debian/forky/amd64", xpg::kDEB);

  // Load one RPM primary.xml index.
  loader.load_packages("data/repos/fedora/44/x86_64.xml", xpg::kRPM);

  // Load all enabled repositories from a repository configuration file.
  // The cache directory must contain files prepared by scripts/download_repository.py.
  loader.load_repositories("data/repos/repoconfig.json", "data/repos");

  // Persist the buffered graph to disk-backed graph storage.
  // Call this before querying data that was just loaded.
  loader.flush_buffer();

  // Build the optional GPU cache.
  // This is required before calling query_dependencies(..., use_gpu = true).
  loader.build_cache();

  // Query package names. The first argument filters by architecture;
  // the second argument is an optional package-name prefix.
  auto packages = graph.query_packages("amd64", "lib");

  // Query versions of a package. The architecture argument is optional.
  auto versions = graph.query_versions("adduser", "all");

  // Query dependencies of adduser 3.134 on all architectures, up to depth 3.
  // The boolean arguments mean:
  //   tree = true
  //   use_gpu = true
  //   match_architecture = true
  //   match_version = true
  //   expand_alternative = true
  auto dependencies = graph.query_dependencies(
    "adduser", "3.134", "all", 3, true, true, true, true, true
  );

  nlohmann::json output;
  output["packages"] = packages;
  output["versions"] = versions;
  output["dependencies"] = dependencies;
  return 0;
}
```

`query_dependencies` returns a `std::variant<xpg::DependencyTree, xpg::DependencyFlat>`. Include `json_serialization.hpp` if you want to serialize the result with `nlohmann::json`.

The most important `query_dependencies` options are:

- `depth`: maximum dependency traversal depth.
- `tree`: return nested dependency tree when true; return per-depth flat levels when false.
- `use_gpu`: use the GPU cache for traversal when true.
- `match_architecture`: filter dependencies by architecture constraints.
- `match_version`: filter dependencies by version constraints.
- `expand_alternative`: expand alternative dependency groups when true.

---

## Use as a Python Module

The build also produces a Python extension module named `pyxpgraph`. The current Python binding is query-only: it can open an existing XPGraph data directory in `load` mode and run package, version, and dependency queries through the `XPGraph` class.

Add the build output directory to `PYTHONPATH` before importing the module:

```sh
export PYTHONPATH="$PWD/build/release/python:$PYTHONPATH"
```

Then open an existing XPGraph data directory and run queries:

```python
from pyxpgraph import XPGraph

graph = XPGraph("data/xpg/example", open_mode="load")

print(graph.package_count())
print(graph.version_count())
print(graph.dependency_count())

total, packages = graph.query_packages(
    limit=20,
    offset=0,
    architecture="amd64",
    prefix="lib",
)

versions = graph.query_versions("adduser", architecture="all")

dependencies = graph.query_dependencies(
    name="adduser",
    version="3.134",
    architecture="all",
    depth=3,
    tree=True,
    use_gpu=False,
    match_architecture=True,
    match_version=True,
    expand_alternative=True,
)

graph.close()
```

`query_packages` returns a tuple: `total_count` is the number of packages matching the architecture and prefix filters before pagination; `visible_packages` contains the requested page after applying `limit` and `offset`.

`query_versions` returns a list of dictionaries with `version` and `architecture` fields. `query_dependencies` returns a Python dictionary in the same structure as the C++ JSON serialization: tree mode returns nested dependencies, while flat mode groups dependencies by depth.

The Python binding currently supports only `open_mode="load"`. It does not expose `PackageLoader`, repository metadata loading, graph creation, or `load-or-create` behavior. Prepare graph data first with the console application, C++ API, or `scripts/reproduce.sh prepare <preset>`, then open the generated data directory from Python.

---

## Testing, Benchmarking, and Profiling

The project includes correctness tests, benchmarks, and profiling helpers. The `scripts/reproduce.sh` wrapper expects the release build to exist and supports predefined data presets:

```sh
bash scripts/reproduce.sh prepare dep5m
bash scripts/reproduce.sh console dep5m
bash scripts/reproduce.sh test dep5m 100
bash scripts/reproduce.sh benchmark dep5m 200
```

Profiling uses the `RelWithDebInfo` build and Linux `perf`:

```sh
bash scripts/reproduce.sh perf dep5m 10 tree cpu 120
bash scripts/reproduce.sh perf dep5m 10 flat gpu 120
```

Generated reports are written under `reports/` by default. Existing benchmark,
test, figure, and profile outputs in this repository are experimental results
for comparing query behavior across data sizes, depths, result formats, and CPU
or GPU execution modes.

---

## Project Status

- Current version: v2.1
- Main API: `xpg::XPGraph` and `xpg::PackageLoader`
- Supported metadata formats: DEB `Packages` and RPM `primary.xml`
- Supported query modes: package list, version list, dependency tree, and
  dependency flat levels
- GPU acceleration is supported through an explicit graph cache
- APIs and storage layout may continue to change as the project evolves
