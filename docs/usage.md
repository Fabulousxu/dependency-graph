# 使用说明

## 1.环境配置

- 需要安装CUDA Toolkit（示例环境为13.1版本）。
- Windows本机或Docker环境均可。

## 2.编译

```powershell
git clone https://github.com/Fabulousxu/dependency-graph.git
mkdir build -Force
cd build
cmake .. -G Ninja
ninja
```

编译好的是提供的一个示例控制台程序`src/console.cpp`，可以直接运行。

## 3.数据准备

### 3.1 下载仓库索引（Packages.gz）

使用 `scripts/download_repository.py` 拉取仓库数据：

- 通过 `repo_infos` 控制下载的发行版、架构和镜像源。
- 通过 `download_repositories(output_dir)` 参数控制输出目录。

### 3.2 生成批处理列表文件

为了批量解析，可以将上述仓库文件路径制作成列表文件（如 jsonl）。

项目内提供 `scripts/generate_dataset.py`，会扫描目录并生成列表文件。

## 4.使用流程

### 4.1 核心类说明

- `DependencyGraph`
  - 构造函数第一个参数为数据库二进制文件在磁盘上的目录位置。
  - 第二个参数为`kLoad`/`kCreate`/`kLoadOrCreate`，区分加载现有数据库或创建新库。
- `PackageLoader`
  - 负责解析`Packages`数据并写入图结构。

### 4.2 典型流程

1. 创建/打开数据库。
2. 用 `PackageLoader` 解析并插入包数据（在内存中）。
3. 调用 `DependencyGraph::flush_buffer()` 将缓冲区落盘，否则无法查询。
4. 调用 `DependencyGraph::build_cache()` 将依赖缓存加载到 GPU（可选但用于 GPU 查询）。
5. 进行查询。

示例代码：

```cpp
#include "DependencyGraph.hpp"
#include "PackageLoader.hpp"
#include <nlohmann/json.hpp>

int main() {
    DependencyGraph graph("./data", kLoadOrCreate);
    PackageLoader loader(graph);

    loader.load_packages_file("Debian-bookworm-amd64");
    loader.load_dataset_file("Debian.jsonl");

    graph.flush_buffer();
    graph.build_cache();

    nlohmann::json result = graph.query_dependencies("adduser", "3.134", "all", 3, true);
    return 0;
}
```

## 5. 注意事项

- 数据插入后会在内存缓冲区中，需要调用`flush_buffer()`，否则查询会失败或为空。
- 若不需要GPU查询，可不调用`build_cache()`，并在查询时将`use_gpu`设为`false`。
