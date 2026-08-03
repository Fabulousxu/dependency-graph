#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>
#include "json_serialization.hpp"
#include "mgxpgraph.hpp"
#include "utils.hpp"
#include "xpgraph.hpp"

namespace xpg {

class QueryDependenciesProfiling {
public:
  int run(int argc, char *argv[]) {
    parse_args(argc, argv);
    init_graph();
    run_profiling();
    return 0;
  }

private:
  XPGraph xpgraph_;
  MGXPGraph mgxpgraph_;
  std::filesystem::path data_directory_;
  std::size_t test_time_ = 0;
  std::size_t depth_ = 0;
  std::string format_;
  bool use_gpu_ = false;
  bool test_memgraph_ = false;
  bool use_query_modules_ = false;
  std::string host_ = "127.0.0.1";
  std::uint16_t port_ = 7687;

  void parse_args(int argc, char *argv[]) {
    CLI::App app("XPGraph query dependencies profile");
    app.add_option("--data-directory", data_directory_, "XPGraph data directory")
       ->required()->check(CLI::ExistingDirectory);
    app.add_option("--test-time", test_time_, "Profile duration in seconds")
       ->required()->check(CLI::PositiveNumber);
    app.add_option("--depth", depth_, "Dependency query depth")->required()->check(CLI::PositiveNumber);
    app.add_option("--format", format_, "Query result format")
       ->required()->check(CLI::IsMember({"tree", "flat"}));
    app.add_flag("--use-gpu", use_gpu_, "Use GPU accelerated query");
    auto *test_memgraph = app.add_flag("--test-memgraph", test_memgraph_, "Query an existing Memgraph instance");
    app.add_option("--host", host_, "Memgraph host")->default_val("127.0.0.1");
    test_memgraph->needs(app.add_option("--port", port_, "Memgraph Bolt port")->check(CLI::Range(1, 65535)));
    app.add_flag("--use-query-modules", use_query_modules_, "Use the Memgraph query module")->needs(test_memgraph);
    try { app.parse(argc, argv); } catch (const CLI::ParseError &error) { std::exit(app.exit(error)); }
  }

  void init_graph() {
    xpgraph_.load(data_directory_);
    if (use_gpu_) xpgraph_.build_cache();
    if (test_memgraph_) {
      mgxpgraph_.connect(host_, port_);
      if (use_query_modules_) mgxpgraph_.load_query_module();
    }
  }

  void run_profiling() {
    std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<std::size_t> distribution(0, xpgraph_.package_count() - 1);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(test_time_);
    while (std::chrono::steady_clock::now() < deadline) {
      auto package = xpgraph_.get_package(distribution(generator));
      if (package.versions().size() <= 2) continue;
      auto _ = query_graph().query_dependencies(
        package.name, "", "", depth_, format_ == "tree", test_memgraph_ ? use_query_modules_ : use_gpu_);
      if (test_memgraph_) mgxpgraph_.clear_arena();
    }
  }

  const GraphBase &query_graph() const {
    if (test_memgraph_) return mgxpgraph_;
    return xpgraph_;
  }
};

} // namespace xpg

int main(int argc, char *argv[]) {
  try { return xpg::QueryDependenciesProfiling().run(argc, argv); } catch (const std::exception &error) {
    xpg::println(std::cerr, "Profiling failed: {}", error.what());
    return 1;
  }
}
