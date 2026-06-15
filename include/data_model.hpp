#pragma once
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "config.hpp"

namespace xpg {

struct PackageView;
struct VersionView;
struct DependencyView;

struct PackageView {
  std::string_view name;
  std::function<std::vector<VersionView>()> versions;
};

struct VersionView {
  std::string_view version;
  std::string_view architecture;
  std::function<std::vector<DependencyView>()> dependencies;
};

struct DependencyView {
  std::function<VersionView()> from_version;
  std::function<PackageView()> to_package;
  std::string_view type;
  std::string_view version_constraint;
  std::string_view architecture_constraint;
  GroupId group;
};

struct VersionInfo {
  std::string_view version;
  std::string_view architecture;
};

struct DependencyTree {
  std::string_view name;
  std::string_view type;
  std::string_view version_constraint;
  std::string_view architecture;
  std::vector<DependencyTree> single_dependencies;
  std::vector<std::vector<DependencyTree>> alternative_dependencies;
};

struct DependencyInfo {
  std::string_view name;
  std::string_view type;
  std::string_view version_constraint;
  std::string_view architecture;
};

struct DependencyLevel {
  std::vector<DependencyInfo> single_dependencies;
  std::vector<std::vector<DependencyInfo>> alternative_dependencies;
};

using DependencyFlat = std::vector<DependencyLevel>;

struct PackageInfo {
  std::string_view name;
  std::string_view version;
  std::string_view architecture;
  std::vector<DependencyInfo> single_dependencies;
  std::vector<std::vector<DependencyInfo>> alternative_dependencies;
};

constexpr bool operator==(const DependencyInfo &l, const DependencyInfo &r) noexcept {
  return l.name == r.name && l.type == r.type
    && l.version_constraint == r.version_constraint && l.architecture == r.architecture;
}

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
  j["architecture"] = tree.architecture;
  j["dependencies"] = {
    {"single_dependencies", tree.single_dependencies},
    {"alternative_dependencies", tree.alternative_dependencies}
  };
}

template <class Json>
void to_json(Json &j, const DependencyInfo &info) {
  j["name"] = info.name;
  j["type"] = info.type;
  j["version_constraint"] = info.version_constraint;
  j["architecture"] = info.architecture;
}

template <class Json>
void to_json(Json &j, const DependencyFlat &flat) {
  for (auto i : std::views::iota(0ull, flat.size()))
    j["depth " + std::to_string(i + 1)] = {
      {"single_dependencies", flat[i].single_dependencies},
      {"alternative_dependencies", flat[i].alternative_dependencies}
    };
}

template <class Json>
void to_json(Json &j, const std::variant<DependencyTree, DependencyFlat> &result) {
  std::visit([&j](const auto &value) { j = value; }, result);
}

} // namespa

template <>
struct std::hash<xpg::DependencyInfo> {
  std::size_t operator()(const xpg::DependencyInfo &item) const noexcept {
    auto h = std::hash<std::string_view>{};
    auto seed = h(item.name);
    seed ^= h(item.type) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= h(item.version_constraint) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= h(item.architecture) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
  }
};
