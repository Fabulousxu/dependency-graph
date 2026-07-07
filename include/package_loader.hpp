#pragma once
#include <cctype>
#include <deque>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <pugixml/pugixml.hpp>
#include "data_model.hpp"
#include "xpgraph.hpp"

namespace xpg {

enum RepositoryType { kDEB, kRPM };

struct RepositoryInfo {
  bool enabled = true;
  RepositoryType type;
  std::vector<std::string> urls;
  std::vector<std::string> distributions;
  std::vector<std::string> architectures;
};

using RepositoryConfig = std::unordered_map<std::string, RepositoryInfo>;

class RpmBoolDependencyParser {
public:
  RpmBoolDependencyParser(std::string_view type, std::string_view arch, std::deque<std::string> &tmpstr)
    noexcept : type_(type), arch_(arch), tmpstr_(tmpstr), pos_(0) {}
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
  std::deque<std::string> &tmpstr_;
  std::string_view rawstr_;
  std::size_t pos_;
  Token token_;

  static bool is_word(char c) noexcept { return !std::isspace(c) && c != '(' && c != ')'; }
  static std::vector<std::vector<DependencyInfo>> to_cnf(const std::variant<Expression, DependencyInfo> &expr);
  Token consume() noexcept;
  std::variant<Expression, DependencyInfo> parse_primary();
  std::variant<Expression, DependencyInfo> parse_expression();
};

class PackageLoader {
public:
  PackageLoader(XPGraph &graph) noexcept : graph_(graph) {}
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

private:
  XPGraph &graph_;

  static PackageInfo parse_deb_package(std::string_view package);
  static DependencyInfo parse_deb_dependency(std::string_view dependency, std::string_view type, std::string_view arch);
  static PackageInfo parse_rpm_package(const pugi::xml_node &package, std::deque<std::string> &tmpstr);
  static std::string_view parse_rpm_version(std::string_view epoch, std::string_view ver, std::string_view rel,
                                            std::deque<std::string> &tmpstr);

  inline static const std::unordered_map<std::string_view, std::string_view> kDebDependencyTypeMap = {
    {"Depends", "DEPENDS"}, {"Pre-Depends", "PRE_DEPENDS"}, {"Recommends", "RECOMMENDS"}, {"Suggests", "SUGGESTS"},
    {"Breaks", "BREAKS"}, {"Conflicts", "CONFLICTS"}, {"Provides", "PROVIDES"}, {"Replaces", "REPLACES"},
    {"Enhances", "ENHANCES"}, {"Built-Using", "BUILT_USING"}, {"Static-Built-Using", "STATIC_BUILT_USING"},
    {"Javascript-Built-Using", "JAVASCRIPT_BUILT_USING"}, {"X-Cargo-Built-Using", "X_CARGO_BUILT_USING"},
  };

  inline static const std::unordered_map<std::string_view, std::string_view> kRpmDependencyTypeMap = {
    {"rpm:requires", "REQUIRES"}, {"rpm:provides", "PROVIDES"}, {"rpm:conflicts", "CONFLICTS"},
    {"rpm:obsoletes", "OBSOLETES"}, {"rpm:recommends", "RECOMMENDS"}, {"rpm:suggests", "SUGGESTS"},
    {"rpm:supplements", "SUPPLEMENTS"}, {"rpm:enhances", "ENHANCES"},
  };

  inline static const std::unordered_map<std::string_view, std::string_view> kRpmFlagsMap =
    {{"LT", "<"}, {"LE", "<="}, {"EQ", "="}, {"GE", ">="}, {"GT", ">"}};
};


} // namespace xpg
