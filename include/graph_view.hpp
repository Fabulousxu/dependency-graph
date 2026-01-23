#pragma once
#include <functional>
#include <string_view>
#include <vector>
#include "config.hpp"

struct PackageView;
struct VersionView;
struct DependencyView;

struct PackageView {
  PackageId id;
  std::string_view name;
  std::function<std::vector<VersionView>()> versions;
};

struct VersionView {
  VersionId id;
  std::string_view version;
  std::string_view architecture;
  std::function<std::vector<DependencyView>()> dependencies;
};

struct DependencyView {
  DependencyId id;
  std::function<VersionView()> from_version;
  std::function<PackageView()> to_package;
  std::string_view dependency_type;
  std::string_view version_constraint;
  std::string_view architecture_constraint;
  GroupId group;
};
