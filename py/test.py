import json
import time
from dependency_graph import DependencyGraph
from package_loader import PackageLoader
from pathlib import Path


def main():
    graph = DependencyGraph()
    loader = PackageLoader(graph)
    load_file_count = 0
    max_load_file_count = 50
    begin = time.time()
    for entry in Path('../repos').iterdir():
        load_file_count += loader.load_from_file(entry, True)
        if load_file_count >= max_load_file_count:
            break
    end = time.time()
    print(f"Finished loading {load_file_count} files. ({end - begin:.3f} s)")
    graph.build_cugraph()
    query_dependencies_performance_test(graph, 100, 10)
    query_console(graph)


def query_console(graph: DependencyGraph):
    while True:
        print('> Query dependencies for package')
        name = input('>   name (type :q to quit): ')
        if name == ':q':
            break
        version = input('>   version (type empty for any): ')
        arch = input('>   architecture (type empty for any): ')
        depth = int(input('>   depth: '))
        use_gpu = input('>   use GPU (y/n): ').lower() == 'y'
        result = graph.query_dependencies(name, version, arch, depth, use_gpu)
        # print(json.dumps(result, indent=2))


def generate_query_list(graph: DependencyGraph, times: int) -> list[str]:
    res: list[str] = []
    for pid in range(0, graph.package_count(), graph.package_count() // times):
        offset = pid
        while True:
            pnode = graph.packages[offset % graph.package_count()]
            if len(pnode.versions) == 0:
                offset += 1
                continue
            res.append(pnode.name)
            break
    return res


def query_dependencies_performance_test(graph: DependencyGraph, times: int, max_depth: int):
    print("=== Query Dependencies Performance Test ===")
    to_query = generate_query_list(graph, times)
    for depth in range(1, max_depth + 1):
        begin = time.time()
        for name in to_query:
            graph.query_dependencies(name, '', '', depth, False)
        end = time.time()
        avg_time = (end - begin) / times
        print(f"[CPU query dependencies] depth={depth}, times={times}: {int(avg_time * 1000000)} us per query.")

        begin = time.time()
        for name in to_query:
            graph.query_dependencies(name, '', '', depth, True)
        end = time.time()
        avg_time = (end - begin) / times
        print(f"<GPU query dependencies> depth={depth}, times={times}: {int(avg_time * 1000000)} us per query.")
    print("===========================================")


if __name__ == '__main__':
    main()
