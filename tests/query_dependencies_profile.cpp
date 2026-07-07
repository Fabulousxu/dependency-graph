#include <chrono>
#include <cstddef>
#include <filesystem>
#include <random>
#include <string>
#include <CLI/CLI11.hpp>
#include "xpgraph.hpp"

struct Option {
  std::filesystem::path data_directory;
  std::size_t test_time;
  std::size_t depth;
  std::string format;
  bool use_gpu;
};

Option parse_args(int argc, char **argv) {
  Option option;
  CLI::App app("XPGraph query dependencies profile");
  app.add_option("--data-directory", option.data_directory, "XPGraph data directory")
     ->required()->check(CLI::ExistingDirectory);
  app.add_option("--test-time", option.test_time, "Profile duration in seconds")
     ->required()->check(CLI::PositiveNumber);
  app.add_option("--depth", option.depth, "Dependency query depth")->required()->check(CLI::PositiveNumber);
  app.add_option("--format", option.format, "Query result format")->required()->check(CLI::IsMember({"tree", "flat"}));
  app.add_flag("--use-gpu", option.use_gpu, "Use GPU accelerated query");
  try { app.parse(argc, argv); } catch (const CLI::ParseError &exc) { std::exit(app.exit(exc)); }
  return option;
}

int main(int argc, char *argv[]) {
  auto [data_directory, test_time, depth, format, use_gpu] = parse_args(argc, argv);
  bool tree = format == "tree";
  xpg::XPGraph graph(data_directory, xpg::open_mode::kLoad);
  if (use_gpu) graph.build_cache();
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<std::size_t> dist(0, graph.package_count() - 1);
  volatile std::size_t sink = 0;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(test_time);
  while (std::chrono::steady_clock::now() < deadline) {
    auto package = graph.get_package(dist(gen));
    if (package.versions().size() <= 2) continue;
    auto result = graph.query_dependencies(package.name, "", "", depth, tree, use_gpu);
    sink += result.index();
  }
  return sink == static_cast<std::size_t>(-1);
}
