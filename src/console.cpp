#include <cstddef>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>
#include "json_serialization.hpp"
#include "package_loader.hpp"
#include "util.hpp"
#include "x_package_graph.hpp"

struct Option {
  xpg::open_mode mode;
  std::filesystem::path data_directory;
  std::filesystem::path repository_config;
  std::filesystem::path cache_directory;
};

Option parse_args(int argc, char **argv) {
  Option option;
  CLI::App app("XPackageGraph interactive console");
  std::unordered_map<std::string, xpg::open_mode> open_mode_map = {
    {"load", xpg::open_mode::kLoad},
    {"create", xpg::open_mode::kCreate},
    {"load-or-create", xpg::open_mode::kLoadOrCreate},
  };
  app.add_option("--open-mode", option.mode, "Open mode: load, create, load-or-create")
     ->transform(CLI::CheckedTransformer(open_mode_map, CLI::ignore_case))->default_val("load-or-create");
  app.add_option("--data-directory", option.data_directory, "Data directory")->default_val("../data");
  app.add_option("--repository-config", option.repository_config, "Repository config file to load");
  app.add_option("--cache-directory", option.cache_directory, "Repository cache directory to load");
  try { app.parse(argc, argv); } catch (const CLI::ParseError &exc) { std::exit(app.exit(exc)); }
  return option;
}

void load_packages(const xpg::PackageLoader &loader) {
  while (true) {
    xpg::println("> Load packages (type ':q' to quit)");
    std::string path;
    xpg::print(">   path: ");
    std::getline(std::cin, path);
    if (path == ":q") return;
    loader.load_deb_packages(path, true, true);
  }
}

void load_repositories(const xpg::PackageLoader &loader) {
  while (true) {
    xpg::println("> Load repositories (type ':q' to quit)");
    std::string config_path, cache_directory;
    xpg::print(">   config path: ");
    std::getline(std::cin, config_path);
    if (config_path == ":q") return;
    xpg::print(">   cache directory: ");
    std::getline(std::cin, cache_directory);
    if (cache_directory == ":q") return;
    try {
      loader.load_repositories(config_path, cache_directory, true, true);
    } catch (const std::exception &e) { xpg::println(std::cerr, "{}", e.what()); }
  }
}

void query_packages(const xpg::XPackageGraph &graph) {
  while (true) {
    xpg::println("> Query packages (type ':q' to quit)");
    std::string limit, offset, architecture, prefix;
    xpg::print(">   limit (default is 0): ");
    std::getline(std::cin, limit);
    if (limit == ":q") return;
    if (limit.empty()) limit = "0";
    xpg::print(">   offset (default is 0): ");
    std::getline(std::cin, offset);
    if (offset == ":q") return;
    if (offset.empty()) offset = "0";
    xpg::print(">   architecture (empty for any): ");
    std::getline(std::cin, architecture);
    if (architecture == ":q") return;
    xpg::print(">   prefix (empty for any): ");
    std::getline(std::cin, prefix);
    if (prefix == ":q") return;
    std::size_t limit_v = std::stoull(limit), offset_v = std::stoull(offset);
    auto result = graph.query_packages(architecture, prefix);
    auto begin = offset_v < result.size() ? result.begin() + offset_v : result.end();
    if (limit_v == 0) limit_v = -1ull;
    auto count = std::min(limit_v, static_cast<std::size_t>(result.end() - begin));
    auto shown = std::ranges::subrange(begin, begin + count);
    nlohmann::ordered_json jresult;
    jresult["total"] = result.size();
    jresult["shown"] = shown.size();
    jresult["offset"] = offset.size();
    jresult["architecture"] = architecture;
    jresult["prefix"] = prefix;
    jresult["packages"] = shown;
    xpg::println("{}", jresult.dump(2));
  }
}

void query_versions(const xpg::XPackageGraph &graph) {
  while (true) {
    xpg::println("> Query versions (type ':q' to quit)");
    std::string name, architecture;
    xpg::print(">   name: ");
    std::getline(std::cin, name);
    if (name == ":q") return;
    xpg::print(">   architecture (empty for any): ");
    std::getline(std::cin, architecture);
    if (architecture == ":q") return;
    nlohmann::ordered_json jresult;
    jresult["package"] = name;
    jresult["architecture"] = architecture;
    jresult["versions"] = graph.query_versions(name, architecture);
    xpg::println("{}", jresult.dump(2));
  }
}

void query_dependencies(const xpg::XPackageGraph &graph) {
  while (true) {
    xpg::println("> Query dependencies (type ':q' to quit)");
    std::string name, version, architecture, depth, format, use_gpu;
    xpg::print(">   name: ");
    std::getline(std::cin, name);
    if (name == ":q") return;
    xpg::print(">   version (empty for any): ");
    std::getline(std::cin, version);
    if (version == ":q") return;
    xpg::print(">   architecture (empty for any): ");
    std::getline(std::cin, architecture);
    if (architecture == ":q") return;
    xpg::print(">   depth (default is 1): ");
    std::getline(std::cin, depth);
    if (depth == ":q") return;
    if (depth.empty()) depth = "1";
    xpg::print(">   format (t/f, tree or flat, default is t): ");
    std::getline(std::cin, format);
    if (format == ":q") return;
    if (format.empty()) format = "t";
    xpg::print(">   use GPU (y/n, default is n): ");
    std::getline(std::cin, use_gpu);
    if (use_gpu == ":q") return;
    if (use_gpu.empty()) use_gpu = "n";
    std::size_t depth_v = std::stoull(depth);
    bool tree_v = format == "t", use_gpu_v = use_gpu == "y";
    nlohmann::ordered_json jresult, &jpackage = jresult["package"];
    jpackage["name"] = name;
    jpackage["version"] = version;
    jpackage["architecture"] = architecture;
    jresult["depth"] = depth;
    jresult["format"] = tree_v ? "tree" : "flat";
    jresult["use_gpu"] = use_gpu_v;
    jresult["dependencies"] = graph.query_dependencies(name, version, architecture, depth_v, tree_v, use_gpu_v);
    xpg::println("{}", jresult.dump(2));
  }
}

int main(int argc, char **argv) {
  auto [mode, data_directory, repository_config, cache_directory] = parse_args(argc, argv);
  xpg::XPackageGraph graph(data_directory, mode);
  xpg::PackageLoader loader(graph);
  if (!repository_config.empty()) {
    loader.load_repositories(repository_config, cache_directory, true, true);
    loader.flush_buffer(false, true);
    loader.build_cache(true);
  }
  while (true) {
    xpg::println("> Select mode");
    xpg::println(">   - 'l' to load packages");
    xpg::println(">   - 'r' to load repositories");
    xpg::println(">   - 'c' to compact storage");
    xpg::println(">   - 'f' to flush buffer");
    xpg::println(">   - 'g' to build GPU cache");
    xpg::println(">   - 'p' to query packages");
    xpg::println(">   - 'v' to query versions");
    xpg::println(">   - 'd' to query dependencies");
    xpg::println(">   - 'q' to quit");
    xpg::print(">   mode: ");
    std::string mode;
    std::getline(std::cin, mode);
    if (mode == "q") break;
    if (mode == "l") load_packages(loader);
    else if (mode == "r") load_repositories(loader);
    else if (mode == "c") loader.compact(true);
    else if (mode == "f") loader.flush_buffer(false, true);
    else if (mode == "g") loader.build_cache(true);
    else if (mode == "p") query_packages(graph);
    else if (mode == "v") query_versions(graph);
    else if (mode == "d") query_dependencies(graph);
  }
  graph.close();
  return 0;
}
