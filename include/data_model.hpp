#pragma once
#include <functional>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include "config.hpp"

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
  std::string_view dependency_type;
  std::string_view version_constraint;
  std::string_view architecture_constraint;
  GroupId group;
};

struct DependencyInfo {
  std::string_view package_name;
  std::string_view dependency_type;
  std::string_view version_constraint;
  std::string_view architecture_constraint;
};

using DependencyGroup = std::vector<DependencyInfo>;

struct DependencyLevel {
  std::vector<DependencyInfo> direct_dependencies;
  std::vector<DependencyGroup> or_dependencies;
};

using DependencyResult = std::vector<DependencyLevel>;

struct PackageInfo {
  std::string_view name;
  std::string_view version;
  std::string_view architecture;
  std::vector<DependencyInfo> direct_dependencies;
  std::vector<DependencyGroup> or_dependencies;
};

constexpr bool operator==(const DependencyInfo &l, const DependencyInfo &r) noexcept {
  return l.package_name == r.package_name && l.dependency_type == r.dependency_type
    && l.version_constraint == r.version_constraint && l.architecture_constraint == r.architecture_constraint;
}

template <>
struct std::hash<DependencyInfo> {
  std::size_t operator()(const DependencyInfo &item) const noexcept {
    auto h = std::hash<std::string_view>{};
    auto seed = h(item.package_name);
    seed ^= h(item.dependency_type) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= h(item.version_constraint) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= h(item.architecture_constraint) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
  }
};

inline void to_json(nlohmann::json &j, const DependencyInfo &info) noexcept {
  j["package_name"] = info.package_name;
  j["type"] = info.dependency_type;
  j["version_constraint"] = info.version_constraint;
  j["architecture_constraint"] = info.architecture_constraint;
}

inline void to_json(nlohmann::ordered_json &j, const DependencyInfo &info) noexcept {
  j["package_name"] = info.package_name;
  j["type"] = info.dependency_type;
  j["version_constraint"] = info.version_constraint;
  j["architecture_constraint"] = info.architecture_constraint;
}

inline void to_json(nlohmann::json &j, const DependencyLevel &dlevel) noexcept {
  j["direct_dependencies"] = dlevel.direct_dependencies;
  j["or_dependencies"] = dlevel.or_dependencies;
}

inline void to_json(nlohmann::ordered_json &j, const DependencyLevel &dlevel) noexcept {
  j["direct_dependencies"] = dlevel.direct_dependencies;
  j["or_dependencies"] = dlevel.or_dependencies;
}

inline DependencyInfo to_item(const DependencyView &dview) {
  DependencyInfo item;
  item.package_name = dview.to_package().name;
  item.dependency_type = dview.dependency_type;
  item.version_constraint = dview.version_constraint;
  item.architecture_constraint = dview.architecture_constraint;
  return item;
}
