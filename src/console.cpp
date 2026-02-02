#define NOMINMAX
#include <nlohmann/json.hpp>
#include "config.hpp"
#include "dependency_graph.hpp"
#include "package_loader.hpp"
#include "util.hpp"

int main() {
  DependencyGraph graph("../data/test", open_mode::kCreate);
  PackageLoader loader(graph);
  std::string dataset_filename;
  print("> Enter dataset filename: ");
  std::cin >> dataset_filename;
  if (!loader.load_dataset_file(dataset_filename, true)) return 1;
  graph.flush_buffer();
  graph.build_cache();
  while (true) {
    println("> Query dependencies for package");
    std::string name, ver, arch, use_gpu;
    print(">   name (type :q to quit): ");
    std::cin >> name;
    if (name == ":q") break;
    print(">   version (type empty for any): ");
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::getline(std::cin, ver);
    print(">   architecture (type empty for any): ");
    std::getline(std::cin, arch);
    print(">   depth: ");
    std::size_t depth;
    std::cin >> depth;
    print(">   use GPU (y/n): ");
    std::cin >> use_gpu;
    nlohmann::ordered_json result = graph.query_dependencies(name, ver, arch, depth, use_gpu == "y" ? 1 : 0);
    println(result.dump(2));
  }
  return 0;
}
