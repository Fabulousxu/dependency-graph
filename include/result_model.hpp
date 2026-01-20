#pragma once
#include <functional>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include "graph_view.hpp"

struct DependencyItem {
  std::string_view package_name;
  std::string_view dependency_type;
  std::string_view version_constraint;
  std::string_view architecture_constraint;
};

using DependencyGroup = std::vector<DependencyItem>;

struct DependencyLevel {
  std::vector<DependencyItem> direct_dependencies;
  std::vector<DependencyGroup> or_dependencies;
};

using DependencyResult = std::vector<DependencyLevel>;

inline bool operator==(const DependencyItem &l, const DependencyItem &r) noexcept {
  return l.package_name == r.package_name && l.dependency_type == r.dependency_type
    && l.version_constraint == r.version_constraint && l.architecture_constraint == r.architecture_constraint;
}

template <>
struct std::hash<DependencyItem> {
  std::size_t operator()(const DependencyItem &item) const noexcept {
    auto h = std::hash<std::string_view>{};
    auto seed = h(item.package_name);
    seed ^= h(item.dependency_type) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= h(item.version_constraint) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return h(item.architecture_constraint) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
  }
};

inline void to_json(nlohmann::json &j, const DependencyItem &item) noexcept {
  j["package_name"] = item.package_name;
  j["type"] = item.dependency_type;
  j["version_constraint"] = item.version_constraint;
  j["architecture_constraint"] = item.architecture_constraint;
}

inline void to_json(nlohmann::ordered_json &j, const DependencyItem &item) noexcept {
  j["package_name"] = item.package_name;
  j["type"] = item.dependency_type;
  j["version_constraint"] = item.version_constraint;
  j["architecture_constraint"] = item.architecture_constraint;
}

inline void to_json(nlohmann::json &j, const DependencyLevel &dlevel) noexcept {
  j["direct_dependencies"] = dlevel.direct_dependencies;
  j["or_dependencies"] = dlevel.or_dependencies;
}

inline void to_json(nlohmann::ordered_json &j, const DependencyLevel &dlevel) noexcept {
  j["direct_dependencies"] = dlevel.direct_dependencies;
  j["or_dependencies"] = dlevel.or_dependencies;
}

inline DependencyItem to_item(const DependencyView &dview) {
  DependencyItem item;
  item.package_name = dview.to_package().name;
  item.dependency_type = dview.dependency_type;
  item.version_constraint = dview.version_constraint;
  item.architecture_constraint = dview.architecture_constraint;
  return item;
}
