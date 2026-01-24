#define NOMINMAX
#include <iostream>
#include <nlohmann/json.hpp>
#include "dependency_graph.hpp"
#include "package_loader.hpp"
#include "util.hpp"

int main() {
  DependencyGraph graph("../data", kLoadOrCreate);
  PackageLoader loader(graph);
  std::string dataset_filename;
  std::cout << "> Enter dataset filename: ";
  std::cin >> dataset_filename;
  if (!loader.load_dataset_file(dataset_filename, true)) return 1;
  graph.flush_buffer();
  graph.sync_gpu();
  while (true) {
    std::cout << "> Query dependencies for package" << std::endl;
    std::string name, ver, arch, use_gpu;
    std::size_t depth;
    std::cout << ">   name (type :q to quit): ";
    std::cin >> name;
    if (name == ":q") break;
    std::cout << ">   version (type empty for any): ";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::getline(std::cin, ver);
    std::cout << ">   architecture (type empty for any): ";
    std::getline(std::cin, arch);
    std::cout << ">   depth: ";
    std::cin >> depth;
    std::cout << ">   use GPU (y/n): ";
    std::cin >> use_gpu;
    nlohmann::ordered_json result = graph.query_dependencies(name, ver, arch, depth, use_gpu == "y" ? 1 : 0);
    std::cout << result.dump(2) << std::endl;
  }
  return 0;
}
