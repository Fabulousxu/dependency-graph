#include <vector>
#include <CLI/CLI11.hpp>
#include "dependency_graph.hpp"
#include "util.hpp"

struct Option {
  std::string load_dir;
  std::size_t trials;
  std::size_t depth;
  bool use_gpu;
};

int main(int argc, char *argv[]) {
  Option opt{};
  CLI::App app;
  app.add_option("--load-dir", opt.load_dir)->required()->check(CLI::ExistingDirectory);
  app.add_option("--trials", opt.trials)->required()->check(CLI::PositiveNumber);
  app.add_option("--depth", opt.depth)->required()->check(CLI::PositiveNumber);
  app.add_flag("--gpu", opt.use_gpu);
  CLI11_PARSE(app, argc, argv);

  DependencyGraph graph;
  if (!graph.load(opt.load_dir)) {
    println("Failed to load DependencyGraph from directory: {}", opt.load_dir);
    return 1;
  }
  println("Total {} packages, {} versions, {} dependencies.",
          graph.package_count(), graph.version_count(), graph.dependency_count());
  print("Building cache on GPU... ");
  auto sync_time = measure_time<std::chrono::milliseconds>([&] { graph.build_cache(); });
  println("Done. ({} s)", sync_time.count() / 1000.0);

  std::vector<std::string> to_query;
  for (std::size_t i = 0; i < graph.package_count(); i += graph.package_count() / opt.trials)
    for (auto j = i;; j++) {
      auto pview = graph.get_package(j % graph.package_count());
      if (pview.versions().empty()) continue;
      to_query.emplace_back(pview.name);
      break;
    }

  print("Querying dependencies for {} packages with depth={}{}... ",
        to_query.size(), opt.depth, opt.use_gpu ? " using GPU" : "");
  auto query_time = measure_time<std::chrono::milliseconds>([&] {
    for (const auto &name : to_query)
      auto _ = graph.query_dependencies(name, "", "", opt.depth, opt.use_gpu);
  });
  println(" Done. ({:.3f} s)", query_time.count() / 1000.0);
  println("Average time per query: {:.3f} ms", query_time.count() / static_cast<double>(to_query.size()));
  return 0;
}
