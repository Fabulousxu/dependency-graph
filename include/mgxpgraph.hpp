#pragma once
#include <cstdint>
#include <deque>
#include <memory>
#include <mgclient.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "config.hpp"
#include "types.hpp"

namespace xpg {

class MGXPGraph : public GraphBase {
  struct VersionNode {
    VersionId id;
    std::string_view version;
    std::string_view architecture;
  };

  struct DependencyEdge {
    PackageId to_package;
    std::string_view name;
    std::string_view type;
    std::string_view version_constraint;
    std::string_view architecture_constraint;
    DependencyGroupId group;
  };

public:
  MGXPGraph() noexcept = default;
  MGXPGraph(std::string_view host, std::uint16_t port, std::string_view username = "",
            std::string_view password = "", bool use_ssl = false) { connect(host, port, username, password, use_ssl); }
  MGXPGraph(const MGXPGraph &) = delete;
  MGXPGraph &operator=(const MGXPGraph &) = delete;
  MGXPGraph(MGXPGraph &&) noexcept = default;
  MGXPGraph &operator=(MGXPGraph &&) noexcept = default;
  ~MGXPGraph() override = default;

  void connect(std::string_view host, std::uint16_t port, std::string_view username = "",
               std::string_view password = "", bool use_ssl = false);
  void close() noexcept override { client_.reset(); }
  bool is_open() const noexcept override { return client_ != nullptr; }

  void load_query_module() const;
  void clear();
  void clear_arena() const { arena_.clear(); }

  std::size_t package_count() const override;
  std::size_t version_count() const override;
  std::size_t dependency_count() const override;

  PackageView get_package(PackageId pid) const override;
  std::optional<PackageView> get_package(std::string_view name) const override;
  bool create_package(const PackageInfo &info, bool update_if_exists = false) override;
  bool delete_package(std::string_view name, std::string_view version, std::string_view architecture) override;

  std::vector<std::string_view> query_packages(std::string_view architecture = "",
                                               std::string_view prefix = "") const override;
  std::vector<VersionInfo> query_versions(std::string_view name, std::string_view architecture = "") const override;
  std::variant<DependencyTree, DependencyFlat> query_dependencies(
    std::string_view name, std::string_view version = "", std::string_view architecture = "", std::size_t depth = 1,
    bool tree = true, bool use_query_modules = false, bool satisfy_architecture = true, bool satisfy_version = true,
    bool expand_alternative = true) const override;

  DependencyTree query_dependency_tree(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
    bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const;
  DependencyFlat query_dependency_flat(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth,
    bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const;
  std::variant<DependencyTree, DependencyFlat> query_dependencies_use_query_modules(
    std::string_view name, std::string_view version, std::string_view architecture, std::size_t depth, bool tree,
    bool satisfy_architecture, bool satisfy_version, bool expand_alternative) const;

private:
  std::unique_ptr<mg::Client> client_;
  mutable std::deque<std::string> arena_;

  void execute_query(std::string_view cypher) const;
  void execute_query(std::string_view cypher, const mg::Map &params) const;
  std::optional<std::int64_t> query_int(std::string_view cypher) const;
  std::optional<std::int64_t> query_int(std::string_view cypher, const mg::Map &params) const;

  VersionView get_version(VersionId vid) const override;
  DependencyView get_dependency(DependencyId did) const override;

  std::pair<PackageId, bool> create_package_node(std::string_view name);
  std::pair<VersionId, bool> create_version_node(PackageId pid, std::string_view version, std::string_view arch,
                                                 bool update_if_exists = false);
  std::pair<VersionId, bool> create_version_node(std::string_view name, std::string_view version, std::string_view arch,
                                                 bool update_if_exists = false);
  std::pair<VersionId, bool> create_version_node(PackageId pid, std::optional<VersionId> old_vid,
                                                 std::string_view version, std::string_view arch,
                                                 bool update_if_exists = false);
  std::pair<DependencyId, bool> create_dependency_edge(VersionId from_vid, PackageId to_pid, std::string_view vcons,
                                                       std::string_view acons, std::string_view type,
                                                       DependencyGroupId group);
  std::pair<DependencyId, bool> create_dependency_edge(VersionId from_vid, std::string_view to_pname,
                                                       std::string_view vcons, std::string_view acons,
                                                       std::string_view type, DependencyGroupId group);

  std::vector<VersionId> init_frontier(std::string_view name, std::string_view version,
                                       std::string_view architecture) const;
};

} // namespace xpg
