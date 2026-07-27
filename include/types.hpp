#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include "config.hpp"

namespace xpg {

enum class open_mode { kLoad, kCreate, kLoadOrCreate };

class XPGraph;
class WriteBuffer;
class CudaCache;
class MGXPGraph;
class PackageLoader;
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
  std::string_view architecture_constraint;
};

struct VersionInfo {
  std::string_view version;
  std::string_view architecture;
};

struct DependencyTree {
  std::string_view name;
  std::string_view type;
  std::string_view version_constraint;
  std::string_view architecture_constraint;
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

enum RepositoryType { kDEB, kRPM };

struct RepositoryInfo {
  bool enabled = true;
  RepositoryType type;
  std::vector<std::string> urls;
  std::vector<std::string> distributions;
  std::vector<std::string> architectures;
};

using RepositoryConfig = std::unordered_map<std::string, RepositoryInfo>;

constexpr bool operator==(const DependencyInfo &l, const DependencyInfo &r) noexcept {
  return l.name == r.name && l.type == r.type && l.version_constraint == r.version_constraint &&
    l.architecture_constraint == r.architecture_constraint;
}

class GraphBase {
public:
  GraphBase() noexcept = default;
  GraphBase(const GraphBase &) = delete;
  GraphBase &operator=(const GraphBase &) = delete;
  GraphBase(GraphBase &&) noexcept = default;
  GraphBase &operator=(GraphBase &&) noexcept = default;
  virtual ~GraphBase() = default;

  virtual void close() = 0;
  virtual bool is_open() const noexcept = 0;
  virtual operator bool() const noexcept { return is_open(); }

  virtual std::size_t package_count() const = 0;
  virtual std::size_t version_count() const = 0;
  virtual std::size_t dependency_count() const = 0;
  virtual bool empty() const { return package_count() == 0; }

  virtual PackageView get_package(PackageId pid) const = 0;
  virtual std::optional<PackageView> get_package(std::string_view name) const = 0;
  virtual bool create_package(const PackageInfo &info, bool update_if_exists = false) = 0;
  virtual bool delete_package(std::string_view name, std::string_view version = "",
                              std::string_view architecture = "") = 0;

  virtual std::vector<std::string_view> query_packages(std::string_view architecture = "",
                                                       std::string_view prefix = "") const = 0;
  virtual std::vector<VersionInfo> query_versions(std::string_view name, std::string_view architecture = "") const = 0;
  virtual std::variant<DependencyTree, DependencyFlat> query_dependencies(
    std::string_view name, std::string_view version = "", std::string_view architecture = "", std::size_t depth = 1,
    bool tree = true, bool use_accelerator = false, bool satisfy_architecture = true, bool satisfy_version = true,
    bool expand_alternative = true) const = 0;

private:
  virtual VersionView get_version(VersionId vid) const = 0;
  virtual DependencyView get_dependency(DependencyId did) const = 0;
};

} // namespace xpg

template <>
struct std::hash<xpg::DependencyInfo> {
  std::size_t operator()(const xpg::DependencyInfo &info) const noexcept {
    auto h = std::hash<std::string_view>{};
    auto seed = h(info.name);
    seed ^= h(info.type) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= h(info.version_constraint) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= h(info.architecture_constraint) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
  }
};
