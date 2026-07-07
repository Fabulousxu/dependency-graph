#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "config.hpp"
#include "device_string_view.hpp"

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
  std::function<std::vector<DependencyView>()> single_dependencies;
  std::function<std::vector<std::vector<DependencyView>>()> alternative_dependencies;
};

struct DependencyView {
  std::function<VersionView()> from_version;
  std::function<PackageView()> to_package;
  std::string_view type;
  std::string_view version_constraint;
  std::string_view architecture;
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
  std::string_view architecture_constraint;
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
  return l.name == r.name && l.type == r.type && l.version_constraint == r.version_constraint
    && l.architecture_constraint == r.architecture_constraint;
}

constexpr HOST_DEVICE bool filter_architecture(ArchitectureId target, ArchitectureId constraint) noexcept {
  if (constraint == kAnyArchitecture) return true;
  return target == kAllArchitecture || target == kNoarchArchitecture || target == constraint;
}

HOST_DEVICE bool filter_version(device_string_view target, device_string_view constraint) noexcept;

} // namespace xpg

template <>
struct std::hash<xpg::DependencyInfo> {
  std::size_t operator()(const xpg::DependencyInfo &item) const noexcept {
    auto h = std::hash<std::string_view>{};
    auto seed = h(item.name);
    seed ^= h(item.type) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= h(item.version_constraint) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= h(item.architecture_constraint) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
  }
};
