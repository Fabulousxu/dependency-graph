#include <cstddef>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>
#include "json_serialization.hpp"
#include "package_loader.hpp"
#include "util.hpp"
#include "xpgraph.hpp"

struct Option {
  xpg::open_mode mode;
  std::filesystem::path data_directory;
  std::filesystem::path repository_config;
  std::filesystem::path cache_directory;
};

Option parse_args(int argc, char **argv) {
  Option option;
  CLI::App app("XPGraph interactive console");
  std::unordered_map<std::string, xpg::open_mode> open_mode_map = {
    {"load", xpg::open_mode::kLoad},
    {"create", xpg::open_mode::kCreate},
    {"load-or-create", xpg::open_mode::kLoadOrCreate},
  };
  app.add_option("--open-mode", option.mode, "Open mode: load, create, load-or-create")
     ->transform(CLI::CheckedTransformer(open_mode_map, CLI::ignore_case))->default_val("load-or-create");
  app.add_option("--data-directory", option.data_directory, "Data directory")->default_val("data");
  app.add_option("--repository-config", option.repository_config, "Repository config file to load");
  app.add_option("--cache-directory", option.cache_directory, "Repository cache directory to load");
  try { app.parse(argc, argv); } catch (const CLI::ParseError &exc) { std::exit(app.exit(exc)); }
  return option;
}

void load_packages(const xpg::PackageLoader &loader) {
  while (true) {
    xpg::println("> Load packages (type ':q' to quit)");
    std::string path, type;
    xpg::print(">   path: ");
    std::getline(std::cin, path);
    if (path == ":q") return;
    xpg::print(">   type (DEB/RPM): ");
    std::getline(std::cin, type);
    if (type == ":q") return;
    if (type == "DEB" || type == "deb") loader.load_packages(path, xpg::kDEB, true, true);
    else if (type == "RPM" || type == "rpm") loader.load_packages(path, xpg::kRPM, true, true);
    else xpg::println(std::cerr, "invalid type: {}", type);
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

void query_packages(const xpg::XPGraph &graph) {
  while (true) {
    xpg::println("> Query packages (type ':q' to quit)");
    std::string architecture, prefix;
    std::size_t limit = 0, offset = 0;
    while (true) {
      std::string limit_input;
      xpg::print(">   limit (default is 0): ");
      std::getline(std::cin, limit_input);
      if (limit_input == ":q") return;
      if (limit_input.empty()) break;
      try {
        std::size_t pos = 0;
        limit = std::stoull(limit_input, &pos);
        if (pos == limit_input.size()) break;
      } catch (const std::exception &) {}
      xpg::println(std::cerr, "invalid limit: {}", limit_input);
    }
    while (true) {
      std::string offset_input;
      xpg::print(">   offset (default is 0): ");
      std::getline(std::cin, offset_input);
      if (offset_input == ":q") return;
      if (offset_input.empty()) break;
      try {
        std::size_t pos = 0;
        offset = std::stoull(offset_input, &pos);
        if (pos == offset_input.size()) break;
      } catch (const std::exception &) {}
      xpg::println(std::cerr, "invalid offset: {}", offset_input);
    }
    xpg::print(">   architecture (empty for any): ");
    std::getline(std::cin, architecture);
    if (architecture == ":q") return;
    xpg::print(">   prefix (empty for any): ");
    std::getline(std::cin, prefix);
    if (prefix == ":q") return;
    auto result = graph.query_packages(architecture, prefix);
    auto begin = offset < result.size() ? result.begin() + offset : result.end();
    auto effective_limit = limit == 0 ? static_cast<std::size_t>(-1) : limit;
    auto count = std::min(effective_limit, static_cast<std::size_t>(result.end() - begin));
    auto shown = std::ranges::subrange(begin, begin + count);
    nlohmann::ordered_json jresult;
    jresult["total"] = result.size();
    jresult["shown"] = shown.size();
    jresult["offset"] = offset;
    jresult["architecture"] = architecture;
    jresult["prefix"] = prefix;
    jresult["packages"] = shown;
    xpg::println("{}", jresult.dump(2));
  }
}

void query_versions(const xpg::XPGraph &graph) {
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

void query_dependencies(const xpg::XPGraph &graph) {
  while (true) {
    xpg::println("> Query dependencies (type ':q' to quit)");
    std::string name, version, architecture;
    std::size_t depth = 1;
    bool tree = true, use_gpu = false, match_architecture = true, match_version = true, expand_alternative = true;
    xpg::print(">   name: ");
    std::getline(std::cin, name);
    if (name == ":q") return;
    xpg::print(">   version (empty for any): ");
    std::getline(std::cin, version);
    if (version == ":q") return;
    xpg::print(">   architecture (empty for any): ");
    std::getline(std::cin, architecture);
    if (architecture == ":q") return;
    while (true) {
      std::string depth_input;
      xpg::print(">   depth (default is 1): ");
      std::getline(std::cin, depth_input);
      if (depth_input == ":q") return;
      if (depth_input.empty()) break;
      try {
        std::size_t pos = 0;
        depth = std::stoull(depth_input, &pos);
        if (pos == depth_input.size()) break;
      } catch (const std::exception &) {}
      xpg::println(std::cerr, "invalid depth: {}", depth_input);
    }
    while (true) {
      std::string format;
      xpg::print(">   format (t/f, tree or flat, default is t): ");
      std::getline(std::cin, format);
      if (format == ":q") return;
      if (format.empty() || format == "t" || format == "tree") {
        tree = true;
        break;
      }
      if (format == "f" || format == "flat") {
        tree = false;
        break;
      }
      xpg::println(std::cerr, "invalid format: {}", format);
    }
    while (true) {
      std::string use_gpu_input;
      xpg::print(">   use GPU (y/n, default is n): ");
      std::getline(std::cin, use_gpu_input);
      if (use_gpu_input == ":q") return;
      if (use_gpu_input.empty() || use_gpu_input == "n" || use_gpu_input == "N" || use_gpu_input == "no") {
        use_gpu = false;
        break;
      }
      if (use_gpu_input == "y" || use_gpu_input == "Y" || use_gpu_input == "yes") {
        use_gpu = true;
        break;
      }
      xpg::println(std::cerr, "invalid use GPU flag: {}", use_gpu_input);
    }
    auto read_bool = [](std::string_view prompt, bool default_value) {
      while (true) {
        std::string input;
        xpg::print("{}", prompt);
        std::getline(std::cin, input);
        if (input == ":q") return std::optional<bool>{};
        if (input.empty()) return std::optional(default_value);
        if (input == "y" || input == "Y" || input == "yes") return std::optional(true);
        if (input == "n" || input == "N" || input == "no") return std::optional(false);
        xpg::println(std::cerr, "invalid flag: {}", input);
      }
    };
    if (auto value = read_bool(">   match architecture (y/n, default is y): ", true); value) match_architecture = *value;
    else return;
    if (auto value = read_bool(">   match version (y/n, default is y): ", true); value) match_version = *value;
    else return;
    if (auto value = read_bool(">   expand alternative dependencies (y/n, default is y): ", true); value)
      expand_alternative = *value;
    else return;
    nlohmann::ordered_json jresult, &jpackage = jresult["package"];
    jpackage["name"] = name;
    jpackage["version"] = version;
    jpackage["architecture"] = architecture;
    jresult["depth"] = depth;
    jresult["format"] = tree ? "tree" : "flat";
    jresult["use_gpu"] = use_gpu;
    jresult["match_architecture"] = match_architecture;
    jresult["match_version"] = match_version;
    jresult["expand_alternative"] = expand_alternative;
    jresult["dependencies"] = graph.query_dependencies(name, version, architecture, depth, tree, use_gpu,
                                                       match_architecture, match_version, expand_alternative);
    xpg::println("{}", jresult.dump(2));
  }
}

int main(int argc, char **argv) {
  auto [mode, data_directory, repository_config, cache_directory] = parse_args(argc, argv);
  xpg::XPGraph graph(data_directory, mode);
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
