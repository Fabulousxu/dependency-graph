#pragma once
#include <cstddef>
#include <concepts>
#include <deque>
#include <ranges>
#include <string>
#include <variant>
#include <nlohmann/json.hpp>
#include "config.hpp"
#include "package_loader.hpp"

namespace xpg {

template <class Json>
void to_json(Json &j, const VersionInfo &info) {
  j["version"] = info.version;
  j["architecture"] = info.architecture;
}

template <class Json>
void to_json(Json &j, const DependencyTree &tree) {
  j["name"] = tree.name;
  j["type"] = tree.type;
  j["version_constraint"] = tree.version_constraint;
  j["architecture"] = tree.architecture_constraint;
  auto &jdependencies = j["dependencies"];
  jdependencies["single_dependencies"] = tree.single_dependencies;
  jdependencies["alternative_dependencies"] = tree.alternative_dependencies;
}

template <class Json>
void to_json(Json &j, const DependencyInfo &info) {
  j["name"] = info.name;
  j["type"] = info.type;
  j["version_constraint"] = info.version_constraint;
  j["architecture"] = info.architecture_constraint;
}

template <class Json>
void to_json(Json &j, const DependencyFlat &flat) {
  for (auto i : std::views::iota(0ull, flat.size())) {
    auto &jlevel = j["depth " + std::to_string(i + 1)];
    jlevel["single_dependencies"] = flat[i].single_dependencies;
    jlevel["alternative_dependencies"] = flat[i].alternative_dependencies;
  }
}

template <class Json>
void to_json(Json &j, const std::variant<DependencyTree, DependencyFlat> &result) {
  std::visit([&j]<class T>(const T &value) {
    if constexpr (std::same_as<T, DependencyTree>) {
      j["single_dependencies"] = value.single_dependencies;
      j["alternative_dependencies"] = value.alternative_dependencies;
    } else if constexpr (std::same_as<T, DependencyFlat>) j = value;
  }, result);
}

template <class Json>
void to_json(Json &j, const RepositoryInfo &info) {
  j["enabled"] = info.enabled;
  if (info.type == kDEB) j["type"] = "DEB";
  else if (info.type == kRPM) j["type"] = "RPM";
  j["urls"] = info.urls;
  j["distributions"] = info.distributions;
  j["architectures"] = info.architectures;
}

template <class Json>
void from_json(const Json &j, DependencyTree &tree, std::deque<std::string> &arena) {
  tree.name = arena.emplace_back(j.at("name").template get<std::string>());
  tree.type = arena.emplace_back(j.at("type").template get<std::string>());
  tree.version_constraint = arena.emplace_back(j.at("version_constraint").template get<std::string>());
  tree.architecture_constraint = arena.emplace_back(j.at("architecture").template get<std::string>());
  const auto &dependencies = j.at("dependencies");
  for (const auto &child : dependencies.at("single_dependencies"))
    from_json(child, tree.single_dependencies.emplace_back(), arena);
  for (const auto &group : dependencies.at("alternative_dependencies")) {
    auto &tree_group = tree.alternative_dependencies.emplace_back();
    for (const auto &child : group) from_json(child, tree_group.emplace_back(), arena);
  }
}

template <class Json>
void from_json(const Json &j, DependencyInfo &info, std::deque<std::string> &arena) {
  info.name = arena.emplace_back(j.at("name").template get<std::string>());
  info.type = arena.emplace_back(j.at("type").template get<std::string>());
  info.version_constraint = arena.emplace_back(j.at("version_constraint").template get<std::string>());
  info.architecture_constraint = arena.emplace_back(j.at("architecture").template get<std::string>());
}

template <class Json>
void from_json(const Json &j, DependencyLevel &level, std::deque<std::string> &arena) {
  for (const auto &item : j.at("single_dependencies"))
    from_json(item, level.single_dependencies.emplace_back(), arena);
  for (const auto &group : j.at("alternative_dependencies")) {
    auto &dependencies = level.alternative_dependencies.emplace_back();
    for (const auto &item : group) from_json(item, dependencies.emplace_back(), arena);
  }
}

template <class Json>
void from_json(const Json &j, DependencyFlat &flat, std::deque<std::string> &arena) {
  for (auto i : std::views::iota(0ull, j.size()))
    from_json(j.at("depth " + std::to_string(i + 1)), flat.emplace_back(), arena);
}

template <class Json>
void from_json(const Json &j, RepositoryInfo &info) {
  info.enabled = j.value("enabled", true);
  std::string type = j.at("type");
  if (type == "DEB" || type == "deb") info.type = kDEB;
  else if (type == "RPM" || type == "rpm") info.type = kRPM;
  info.urls = j.at("urls");
  info.distributions = j.value("distributions", decltype(info.distributions){});
  info.architectures = j.value("architectures", decltype(info.architectures){});
}

} // namespace xpg
