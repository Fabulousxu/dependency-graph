#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>
#include "json_serialization.hpp"
#include "mgxpgraph.hpp"
#include "package_loader.hpp"
#include "utils.hpp"
#include "xpgraph.hpp"

struct Option {
  xpg::open_mode mode;
  std::filesystem::path data_directory;
  std::filesystem::path repository_config;
  std::filesystem::path cache_directory;
  bool use_memgraph = false;
  std::string host;
  std::uint16_t port;
};

Option parse_args(int argc, char **argv) {
  Option option{};
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
  app.add_flag("--use-memgraph", option.use_memgraph, "Use Memgraph backend");
  app.add_option("--host", option.host, "Memgraph host")->default_val("127.0.0.1");
  app.add_option("--port", option.port, "Memgraph Bolt port")->default_val(7687)->check(CLI::Range(1, 65535));
  try { app.parse(argc, argv); } catch (const CLI::ParseError &exc) { std::exit(app.exit(exc)); }
  return option;
}

std::optional<std::string> input_string(std::string_view prompt,
                                        std::optional<std::string_view> default_value = std::nullopt) {
  while (true) {
    std::string input;
    xpg::print("{}", prompt);
    std::getline(std::cin, input);
    if (input == ":q") return std::nullopt;
    if (!input.empty()) return input;
    if (default_value) return std::string(*default_value);
    xpg::println(std::cerr, "empty input is not allowed");
  }
}

std::optional<std::int64_t> input_int(std::string_view prompt,
                                      std::optional<std::int64_t> default_value = std::nullopt) {
  while (true) {
    auto input = input_string(prompt, default_value ? std::optional<std::string_view>{""} : std::nullopt);
    if (!input) return std::nullopt;
    if (input->empty()) return *default_value;
    try {
      std::size_t pos = 0;
      auto value = std::stoll(*input, &pos);
      if (pos == input->size()) return value;
    } catch (const std::exception &) {}
    xpg::println(std::cerr, "invalid integer: {}", *input);
  }
}

std::optional<bool> input_bool(std::string_view prompt, std::optional<bool> default_value,
                               std::span<const std::string_view> true_inputs,
                               std::span<const std::string_view> false_inputs) {
  while (true) {
    auto input = input_string(prompt, default_value ? std::optional<std::string_view>{""} : std::nullopt);
    if (!input) return std::nullopt;
    if (input->empty()) return *default_value;
    if (std::ranges::find(true_inputs, *input) != true_inputs.end()) return true;
    if (std::ranges::find(false_inputs, *input) != false_inputs.end()) return false;
    xpg::println(std::cerr, "invalid flag: {}", *input);
  }
}
std::optional<bool> input_bool(std::string_view prompt, std::optional<bool> default_value = std::nullopt,
                               std::initializer_list<std::string_view> true_inputs = {"y", "Y", "yes"},
                               std::initializer_list<std::string_view> false_inputs = {"n", "N", "no"}) {
  return input_bool(prompt, default_value, std::span(true_inputs), std::span(false_inputs));
}

void load_packages(const xpg::PackageLoader &loader) {
  while (true) {
    xpg::println("> Load packages (type ':q' to quit)");
    auto path = input_string(">   path: ");
    if (!path) return;
    auto type = input_string(">   type (DEB/RPM): ");
    if (!type) return;
    std::optional<xpg::RepositoryType> repository_type;
    if (*type == "DEB" || *type == "deb") repository_type = xpg::kDEB;
    else if (*type == "RPM" || *type == "rpm") repository_type = xpg::kRPM;
    else {
      xpg::println(std::cerr, "invalid type: {}", *type);
      continue;
    }
    loader.load_packages(*path, *repository_type, true, true);
  }
}

void load_repositories(const xpg::PackageLoader &loader) {
  while (true) {
    xpg::println("> Load repositories (type ':q' to quit)");
    auto config_path = input_string(">   config path: ");
    if (!config_path) return;
    auto cache_directory = input_string(">   cache directory: ");
    if (!cache_directory) return;
    try {
      loader.load_repositories(*config_path, *cache_directory, true, true);
    } catch (const std::exception &e) { xpg::println(std::cerr, "{}", e.what()); }
  }
}

void query_packages(const xpg::GraphBase &graph) {
  while (true) {
    xpg::println("> Query packages (type ':q' to quit)");
    auto architecture = input_string(">   architecture (empty for any): ", "");
    if (!architecture) return;
    auto prefix = input_string(">   prefix (empty for any): ", "");
    if (!prefix) return;
    auto limit = input_int(">   limit (default is 0): ", 0);
    if (!limit) return;
    auto offset = input_int(">   offset (default is 0): ", 0);
    if (!offset) return;
    auto result = graph.query_packages(*architecture, *prefix);
    auto begin = *offset < result.size() ? result.begin() + *offset : result.end();
    if (*limit == 0) limit = -1ull;
    auto count = std::min(static_cast<std::size_t>(*limit), static_cast<std::size_t>(result.end() - begin));
    auto shown = std::ranges::subrange(begin, begin + count);
    nlohmann::ordered_json jresult;
    jresult["total"] = result.size();
    jresult["shown"] = shown.size();
    jresult["offset"] = offset;
    jresult["architecture"] = *architecture;
    jresult["prefix"] = *prefix;
    jresult["packages"] = shown;
    xpg::println("{}", jresult.dump(2));
  }
}

void query_versions(const xpg::GraphBase &graph) {
  while (true) {
    xpg::println("> Query versions (type ':q' to quit)");
    auto name = input_string(">   name: ");
    if (!name) return;
    auto architecture = input_string(">   architecture (empty for any): ", "");
    if (!architecture) return;
    nlohmann::ordered_json jresult;
    jresult["package"] = *name;
    jresult["architecture"] = *architecture;
    jresult["versions"] = graph.query_versions(*name, *architecture);
    xpg::println("{}", jresult.dump(2));
  }
}

void query_dependencies(const xpg::GraphBase &graph, bool use_memgraph) {
  while (true) {
    xpg::println("> Query dependencies (type ':q' to quit)");
    auto name = input_string(">   name: ");
    if (!name) return;
    auto version = input_string(">   version (empty for any): ", "");
    if (!version) return;
    auto architecture = input_string(">   architecture (empty for any): ", "");
    if (!architecture) return;
    auto depth = input_int(">   depth (default is 1): ", 1);
    if (!depth) return;
    auto tree = input_bool(">   format (t/f, tree or flat, default is t): ", true, {"t", "tree"}, {"f", "flat"});
    if (!tree) return;
    auto use_accelerator = input_bool(use_memgraph ? ">   use query module (y/n, default is n): " :
                                        ">   use GPU (y/n, default is n): ", false);
    if (!use_accelerator) return;
    auto satisfy_architecture = input_bool(">   satisfy architecture (y/n, default is y): ", true);
    if (!satisfy_architecture) return;
    auto satisfy_version = input_bool(">   satisfy version (y/n, default is y): ", true);
    if (!satisfy_version) return;
    auto expand_alternative = input_bool(">   expand alternative dependencies (y/n, default is y): ", true);
    if (!expand_alternative) return;
    nlohmann::ordered_json jresult, &jpackage = jresult["package"];
    jpackage["name"] = *name;
    jpackage["version"] = *version;
    jpackage["architecture"] = *architecture;
    jresult["depth"] = depth;
    jresult["format"] = tree ? "tree" : "flat";
    jresult[use_memgraph ? "use_query_modules" : "use_gpu"] = *use_accelerator;
    jresult["satisfy_architecture"] = satisfy_architecture;
    jresult["satisfy_version"] = satisfy_version;
    jresult["expand_alternative"] = expand_alternative;
    jresult["dependencies"] = graph.query_dependencies(*name, *version, *architecture, *depth, *tree, *use_accelerator,
                                                       *satisfy_architecture, *satisfy_version, *expand_alternative);
    xpg::println("{}", jresult.dump(2));
  }
}

int main(int argc, char **argv) {
  auto option = parse_args(argc, argv);
  std::unique_ptr<xpg::GraphBase> graph;
  if (option.use_memgraph) {
    auto mgxpgraph = std::make_unique<xpg::MGXPGraph>();
    mgxpgraph->connect(option.host, option.port);
    if (option.mode == xpg::open_mode::kCreate) mgxpgraph->clear();
    graph = std::move(mgxpgraph);
  } else {
    auto xpgraph = std::make_unique<xpg::XPGraph>();
    xpgraph->open(option.data_directory, option.mode);
    graph = std::move(xpgraph);
  }
  xpg::PackageLoader loader(*graph);
  if (!option.repository_config.empty()) {
    auto _ = loader.load_repositories(option.repository_config, option.cache_directory, true, true);
    if (dynamic_cast<xpg::XPGraph *>(graph.get())) {
      loader.flush_buffer(false, true);
      loader.build_cache(true);
    }
  }
  while (true) {
    xpg::println("> Select mode");
    xpg::println(">   - 'l' to load packages");
    xpg::println(">   - 'r' to load repositories");
    if (dynamic_cast<xpg::XPGraph *>(graph.get())) {
      xpg::println(">   - 'c' to compact storage");
      xpg::println(">   - 'f' to flush buffer");
      xpg::println(">   - 'g' to build GPU cache");
    } else if (dynamic_cast<xpg::MGXPGraph *>(graph.get()))
      xpg::println(">   - 'c' to clear memgraph");
    xpg::println(">   - 'p' to query packages");
    xpg::println(">   - 'v' to query versions");
    xpg::println(">   - 'd' to query dependencies");
    xpg::println(">   - 'q' to quit");
    auto mode = input_string(">   mode: ");
    if (!mode) break;
    if (*mode == "q") break;
    if (*mode == "l") load_packages(loader);
    else if (*mode == "r") load_repositories(loader);
    else if (dynamic_cast<xpg::XPGraph *>(graph.get()) && *mode == "c") loader.compact(true);
    else if (dynamic_cast<xpg::XPGraph *>(graph.get()) && *mode == "f") loader.flush_buffer(false, true);
    else if (dynamic_cast<xpg::XPGraph *>(graph.get()) && *mode == "g") loader.build_cache(true);
    else if (dynamic_cast<xpg::MGXPGraph *>(graph.get()) && *mode == "c") loader.clear(true);
    else if (*mode == "p") query_packages(*graph);
    else if (*mode == "v") query_versions(*graph);
    else if (*mode == "d") query_dependencies(*graph, option.use_memgraph);
    else xpg::println(std::cerr, "invalid mode: {}", *mode);
  }
  graph->close();
  return 0;
}
