#pragma once
#include <cctype>
#include <deque>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <pugixml/pugixml.hpp>
#include "config.hpp"
#include "types.hpp"

namespace xpg {

class RpmBoolDependencyParser {
public:
  RpmBoolDependencyParser(std::string_view type, std::string_view arch, std::deque<std::string> &arena) noexcept
    : type_(type), arch_(arch), pos_(0), arena_(arena) {}
  std::vector<std::vector<DependencyInfo>> parse(std::string_view rawstr);

private:
  enum TokenType { kEnd, kLeftParen, kRightParen, kOperator, kComparison, kWord };
  enum Operator { kAnd, kOr, kIf, kIfElse, kWith, kWithout, kUnless, kUnlessElse, kElse };

  struct Token {
    TokenType type = kEnd;
    std::variant<Operator, std::string_view> value;
  };

  struct Expression {
    Operator operator_ = kAnd;
    std::vector<std::variant<Expression, DependencyInfo>> operands;
  };

  std::string_view type_;
  std::string_view arch_;
  std::string_view rawstr_;
  std::size_t pos_;
  Token token_;
  std::deque<std::string> &arena_;

  static bool is_word(char c) noexcept { return !std::isspace(c) && c != '(' && c != ')'; }
  static std::vector<std::vector<DependencyInfo>> to_cnf(const std::variant<Expression, DependencyInfo> &expr);
  Token consume() noexcept;
  std::variant<Expression, DependencyInfo> parse_primary();
  std::variant<Expression, DependencyInfo> parse_expression();
};

class PackageLoader {
public:
  PackageLoader(GraphBase &graph) noexcept : graph_(graph) {}
  PackageLoader(const PackageLoader &) noexcept = default;
  PackageLoader &operator=(const PackageLoader &) = delete;
  PackageLoader(PackageLoader &&) noexcept = default;
  PackageLoader &operator=(PackageLoader &&) = delete;
  ~PackageLoader() = default;

  bool load_packages(const std::filesystem::path &path, RepositoryType type, bool flush_if_needed = true,
                     bool verbose = false) const;
  std::pair<std::size_t, std::size_t> load_repositories(const RepositoryConfig &config,
                                                        const std::filesystem::path &cache_dir = "",
                                                        bool flush_if_needed = true, bool verbose = false) const;
  std::pair<std::size_t, std::size_t> load_repositories(const std::filesystem::path &config_path,
                                                        const std::filesystem::path &cache_dir = "",
                                                        bool flush_if_needed = true, bool verbose = false) const;

  void flush_buffer(bool update_if_exists = false, bool verbose = false) const;
  void build_cache(bool verbose = false) const;
  void compact(bool verbose = false) const;
  void clear(bool verbose = false) const;

private:
  GraphBase &graph_;
  mutable std::deque<std::string> arena_;

  static PackageInfo parse_deb_package(std::string_view package);
  static DependencyInfo parse_deb_dependency(std::string_view dependency, std::string_view type, std::string_view arch);
  PackageInfo parse_rpm_package(const pugi::xml_node &package) const;
  std::string_view parse_rpm_version(std::string_view epoch, std::string_view ver, std::string_view rel) const;
};

} // namespace xpg
