#include "package_loader.hpp"
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cuda_runtime.h>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include "config.hpp"
#include "json_serialization.hpp"
#include "util.hpp"

namespace xpg {

struct GpuMemInfo {
  std::size_t free_bytes;
  std::size_t total_bytes;
};

GpuMemInfo getGpuMemInfo() {
  GpuMemInfo result;
  auto code = cudaMemGetInfo(&result.free_bytes, &result.total_bytes);
  if (code != cudaSuccess) throw std::runtime_error(cudaGetErrorString(code));
  return result;
}

std::vector<std::vector<DependencyInfo>> RpmBoolDependencyParser::parse(std::string_view rawstr) {
  rawstr_ = rawstr;
  pos_ = 0;
  token_ = consume();
  try { return to_cnf(parse_expression()); } catch (const std::exception &) { return {}; }
}

std::vector<std::vector<DependencyInfo>> RpmBoolDependencyParser::to_cnf(
  const std::variant<Expression, DependencyInfo> &expr) {
  if (auto *info = std::get_if<DependencyInfo>(&expr)) return {{*info}};
  const auto &[operator_, operands] = std::get<Expression>(expr);
  if (operands.empty()) return {};
  auto result = to_cnf(operands.front());
  switch (operator_) {
  case kAnd: // A and (B and C) -> A and B and C
  case kWith: // A with B -> A and B
    for (auto i : std::views::iota(1ull, operands.size())) {
      auto cnf = to_cnf(operands[i]);
      result.insert(result.end(), std::make_move_iterator(cnf.begin()), std::make_move_iterator(cnf.end()));
    }
    break;
  case kOr: // A or (B and C) -> (A or B) and (A or C)
  case kUnless: // A unless B -> (not B and A) or B -> A or B
    for (auto i : std::views::iota(1ull, operands.size())) {
      auto cnf = to_cnf(operands[i]);
      std::vector<std::vector<DependencyInfo>> distributed;
      distributed.reserve(result.size() * cnf.size());
      for (const auto &lclause : result)
        for (const auto &rclause : cnf) {
          auto &clause = distributed.emplace_back(lclause);
          clause.insert(clause.end(), rclause.begin(), rclause.end());
        }
      result = std::move(distributed);
    }
    break;
  case kIfElse: // A if B else C -> A or C
  case kUnlessElse: { // A unless B else C -> A or C
    if (operands.size() < 3) throw std::runtime_error("Missing else operand in RPM boolean dependency expression.");
    auto cnf = to_cnf(operands[2]);
    std::vector<std::vector<DependencyInfo>> distributed;
    distributed.reserve(result.size() * cnf.size());
    for (const auto &lclause : result)
      for (const auto &rclause : cnf) {
        auto &clause = distributed.emplace_back(lclause);
        clause.insert(clause.end(), rclause.begin(), rclause.end());
      }
    result = std::move(distributed);
    break;
  }
  case kIf: // A if B -> (B and A) or not B -> A or not B -> A
  case kWithout: // A without B -> A and not B -> A
  case kElse: // unreachable
    break;
  }
  return result;
}

RpmBoolDependencyParser::Token RpmBoolDependencyParser::consume() noexcept {
  while (pos_ < rawstr_.size() && std::isspace(rawstr_[pos_])) ++pos_;
  if (pos_ >= rawstr_.size()) return {kEnd, rawstr_.substr(rawstr_.size())};
  if (rawstr_[pos_] == '(') return {kLeftParen, rawstr_.substr(pos_++, 1)};
  if (rawstr_[pos_] == ')') return {kRightParen, rawstr_.substr(pos_++, 1)};
  auto begin = pos_;
  while (pos_ < rawstr_.size() && is_word(rawstr_[pos_])) ++pos_;
  while (pos_ < rawstr_.size() && rawstr_[pos_] == '(') {
    auto depth = 0;
    do {
      if (rawstr_[pos_] == '(') ++depth;
      else if (rawstr_[pos_] == ')') --depth;
      ++pos_;
    } while (pos_ < rawstr_.size() && depth > 0);
    while (pos_ < rawstr_.size() && is_word(rawstr_[pos_])) ++pos_;
  }
  auto word = rawstr_.substr(begin, pos_ - begin);
  if (word == "<" || word == "<=" || word == "=" || word == ">=" || word == ">") return {kComparison, word};
  if (word == "and") return {kOperator, kAnd};
  if (word == "or") return {kOperator, kOr};
  if (word == "if") return {kOperator, kIf};
  if (word == "unless") return {kOperator, kUnless};
  if (word == "with") return {kOperator, kWith};
  if (word == "without") return {kOperator, kWithout};
  if (word == "else") return {kOperator, kElse};
  return {kWord, word};
}

auto RpmBoolDependencyParser::parse_primary() -> std::variant<Expression, DependencyInfo> {
  if (token_.type == kLeftParen) {
    token_ = consume();
    auto expr = parse_expression();
    if (token_.type != kRightParen) throw std::runtime_error("Expected ')' in RPM boolean dependency expression.");
    token_ = consume();
    return expr;
  }
  if (token_.type != kWord) throw std::runtime_error("Expected RPM dependency name.");
  DependencyInfo info;
  info.name = std::get<std::string_view>(token_.value);
  info.type = type_;
  info.architecture_constraint = arch_;
  token_ = consume();
  if (token_.type == kComparison) {
    auto cmp = std::string(std::get<std::string_view>(token_.value)).append(" ");
    token_ = consume();
    if (token_.type != kWord) throw std::runtime_error("Expected RPM dependency version.");
    info.version_constraint = tmpstr_.emplace_back(std::move(cmp.append(std::get<std::string_view>(token_.value))));
    token_ = consume();
  }
  return info;
}

auto RpmBoolDependencyParser::parse_expression() -> std::variant<Expression, DependencyInfo> {
  auto result = parse_primary();
  while (token_.type == kOperator) {
    auto op = std::get<Operator>(token_.value);
    if (op == kElse) throw std::runtime_error("Unexpected 'else' in RPM boolean dependency expression.");
    token_ = consume();
    Expression expr{.operator_ = op};
    if (auto *left = std::get_if<Expression>(&result); (op == kAnd || op == kOr) && left && left->operator_ == op)
      expr = std::move(*left);
    else expr.operands.emplace_back(std::move(result));
    expr.operands.emplace_back(parse_primary());
    if ((op == kIf || op == kUnless) && token_.type == kOperator) {
      if (std::get<Operator>(token_.value) == kElse) {
        token_ = consume();
        expr.operator_ = op == kIf ? kIfElse : kUnlessElse;
        expr.operands.emplace_back(parse_primary());
      } else throw std::runtime_error("Expected 'else' after RPM boolean dependency if/unless expression.");
    }
    result = std::move(expr);
  }
  return result;
}

PackageInfo PackageLoader::parse_deb_package(std::string_view package) {
  std::unordered_map<std::string_view, std::string_view> fields;
  for (auto line : package | std::views::split('\n')) {
    if (line.empty()) continue;
    std::string_view lview(line.begin(), line.end());
    if (auto colon = lview.find(':'); colon != std::string_view::npos)
      fields.emplace(trim(lview.substr(0, colon)), trim(lview.substr(colon + 1)));
  }
  PackageInfo result;
  result.name = fields.at("Package");
  if (result.name.empty()) throw std::runtime_error("Missing DEB package name.");
  result.version = fields.at("Version");
  result.architecture = fields.at("Architecture");
  for (const auto &[field, type] : kDebDependencyTypeMap)
    if (auto it = fields.find(field); it != fields.end())
      for (auto group : it->second | std::views::split(','))
        if (auto items = group | std::views::split('|'); std::ranges::distance(items) > 1) {
          result.alternative_dependencies.emplace_back();
          for (auto item : items) {
            auto info = parse_deb_dependency({item.begin(), item.end()}, type, result.architecture);
            result.alternative_dependencies.back().emplace_back(std::move(info));
          }
        } else {
          auto info = parse_deb_dependency({group.begin(), group.end()}, type, result.architecture);
          result.single_dependencies.emplace_back(std::move(info));
        }
  return result;
}

DependencyInfo PackageLoader::parse_deb_dependency(std::string_view dependency, std::string_view type,
                                                   std::string_view arch) {
  DependencyInfo info{.type = type};
  auto lpar = dependency.find('(');
  if (lpar != std::string_view::npos) {
    auto rpar = dependency.rfind(')');
    if (rpar != std::string_view::npos && rpar > lpar)
      info.version_constraint = trim(dependency.substr(lpar + 1, rpar - lpar - 1));
    else throw std::runtime_error("Expected after in DEB depencency version constraint");
  }
  dependency = dependency.substr(0, lpar);
  auto colon = dependency.find(':');
  info.name = trim(dependency.substr(0, colon));
  if (info.name.empty()) throw std::runtime_error("Missing DEB dependency name.");
  info.architecture_constraint = colon != std::string_view::npos ? trim(dependency.substr(colon + 1)) : arch;
  return info;
}

PackageInfo PackageLoader::parse_rpm_package(const pugi::xml_node &package, std::deque<std::string> &tmpstr) {
  PackageInfo result;
  result.name = package.child("name").child_value();
  if (result.name.empty()) throw std::runtime_error("Missing RPM package name.");
  result.architecture = package.child("arch").child_value();
  auto version = package.child("version");
  result.version = parse_rpm_version(version.attribute("epoch").value(), version.attribute("ver").value(),
                                     version.attribute("rel").value(), tmpstr);
  auto format = package.child("format");
  if (!format) return result;
  for (const auto &[field, type] : kRpmDependencyTypeMap)
    if (auto dependencies = format.child(field.data()))
      for (auto entry : dependencies.children("rpm:entry")) {
        auto name = std::string_view(entry.attribute("name").value());
        if (name.starts_with('(')) {
          for (auto group : RpmBoolDependencyParser(type, result.architecture, tmpstr).parse(name))
            if (group.size() == 1) {
              result.alternative_dependencies.emplace_back();
              for (auto &info : group) result.alternative_dependencies.back().emplace_back(std::move(info));
            } else result.single_dependencies.emplace_back(group.front());
        } else {
          DependencyInfo info;
          info.name = name;
          info.type = type;
          info.architecture_constraint = result.architecture;
          auto flags = std::string_view(entry.attribute("flags").value());
          if (!flags.empty()) {
            auto it = kRpmFlagsMap.find(flags);
            if (it == kRpmFlagsMap.end())
              throw std::runtime_error(std::format("Unknown RPM dependency flags: {}", flags));
            auto version = parse_rpm_version(
              entry.attribute("epoch").value(), entry.attribute("ver").value(), entry.attribute("rel").value(), tmpstr);
            info.version_constraint = tmpstr.emplace_back(std::format("{} {}", it->second, version));
          }
          result.single_dependencies.emplace_back(std::move(info));
        }
      }
  return result;
}

std::string_view PackageLoader::parse_rpm_version(std::string_view epoch, std::string_view ver, std::string_view rel,
                                                  std::deque<std::string> &tmpstr) {
  std::string result;
  if (!epoch.empty() && epoch != "0") result.append(epoch).append(":");
  result.append(ver);
  if (!rel.empty()) result.append("-").append(rel);
  return tmpstr.emplace_back(std::move(result));
}

bool PackageLoader::load_packages(const std::filesystem::path &path, RepositoryType type, bool flush_if_needed,
                                  bool verbose) const {
  std::ifstream deb_file;
  pugi::xml_document rpm_doc;
  pugi::xml_node rpm_root;
  if (type == kDEB) {
    deb_file.open(path, std::ios::binary);
    if (!deb_file.good()) {
      if (verbose) println(std::cerr, "Failed to open DEB packages file: {}.", path.string());
      return false;
    }
  } else {
    if (auto result = rpm_doc.load_file(path.c_str()); !result) {
      if (verbose)
        println(std::cerr, "Failed to open RPM primary XML file: {}. {}", path.string(), result.description());
      return false;
    }
    rpm_root = rpm_doc.child("metadata");
    if (rpm_root.empty()) {
      if (verbose) println(std::cerr, "Failed to find <metadata> in RPM primary XML file: {}.", path.string());
      return false;
    }
  }
  auto pcount = graph_.buffer_package_count();
  auto vcount = graph_.buffer_version_count();
  auto dcount = graph_.buffer_dependency_count();
  if (verbose) print("Loading {} file: {}... ", type == kDEB ? "DEB packages" : "RPM primary XML", path.string());
  auto time = measure_time<std::chrono::milliseconds>([&, this, type, verbose] {
    if (type == kDEB) {
      deb_file.seekg(0, std::ios::end);
      auto file_size = deb_file.tellg();
      deb_file.seekg(0);
      std::string pkgs(file_size, '\0');
      deb_file.read(pkgs.data(), file_size);
      for (auto pkg : pkgs | std::views::split(std::string_view("\n\n"))) {
        auto view = std::string_view{pkg.begin(), pkg.end()};
        if (view.empty()) continue;
        try {
          graph_.create_package(parse_deb_package(view));
        } catch (const std::exception &e) {
          if (verbose) println(std::cerr, "  Skipped malformed DEB package: {}", e.what());
        }
      }
    } else
      for (auto pkg : rpm_root.children("package")) {
        std::deque<std::string> tmpstr;
        try {
          graph_.create_package(parse_rpm_package(pkg, tmpstr));
        } catch (const std::exception &e) {
          if (verbose) println(std::cerr, "  Skipped malformed RPM package: {}", e.what());
        }
      }
  });
  if (verbose) println("Done. ({} ms)", time.count());
  pcount = graph_.buffer_package_count() - pcount;
  vcount = graph_.buffer_version_count() - vcount;
  dcount = graph_.buffer_dependency_count() - dcount;
  if (flush_if_needed)
    if (auto memory_usage = graph_.estimated_memory_usage(); memory_usage >= graph_.memory_limit()) {
      if (verbose)
        print("  Estimated memory usage {:.1f} MiB exceeded limit {} MiB.\n  Flushing to disk... ",
              memory_usage / MiBd, graph_.memory_limit() / MiB);
      auto flush_time = measure_time<std::chrono::milliseconds>([this] { graph_.flush_buffer(); });
      if (verbose) println("Done. ({:.3f} s)", flush_time.count() / 1000.0);
    }
  if (verbose) {
    println("  Loaded: + {} packages, + {} versions, + {} dependencies.", pcount, vcount, dcount);
    println("  Total: {} packages, {} versions, {} dependencies (buffer)",
            graph_.buffer_package_count(), graph_.buffer_version_count(), graph_.buffer_dependency_count());
    println("         {} packages, {} versions, {} dependencies (disk)",
            graph_.package_count(), graph_.version_count(), graph_.dependency_count());
  }
  return true;
}

std::pair<std::size_t, std::size_t> PackageLoader::load_repositories(
  const RepositoryConfig &config, const std::filesystem::path &cache_dir, bool flush_if_needed, bool verbose) const {
  std::size_t total = 0, loaded = 0;
  auto time = measure_time<std::chrono::milliseconds>([&, this] {
    for (const auto &[name, repo] : config) {
      if (!repo.enabled) {
        if (verbose) println("Skipping disabled repository: {}", name);
        continue;
      }
      for (const auto &dist : repo.distributions)
        for (const auto &arch : repo.architectures) {
          std::filesystem::path path;
          if (repo.type == kDEB) path = cache_dir / name / dist / arch;
          else if (repo.type == kRPM) path = cache_dir / name / dist / (arch + ".xml");
          std::error_code ec;
          if (!std::filesystem::is_regular_file(path, ec)) {
            if (verbose) {
              println("Cached repository package file not found: {}.", path.string());
              println(std::cerr, "  Network download is not supported yet. Skipping");
            }
            continue;
          }
          ++total;
          if (load_packages(path, repo.type, flush_if_needed, verbose)) ++loaded;
        }
    }
  });
  if (verbose) println("Loaded repositories ({:.3f} s). {}/{} loaded", time.count() / 1000.0, loaded, total);
  return {total, loaded};
}

std::pair<std::size_t, std::size_t> PackageLoader::load_repositories(const std::filesystem::path &config_path,
                                                                     const std::filesystem::path &cache_dir,
                                                                     bool flush_if_needed, bool verbose) const {
  std::ifstream file(config_path);
  if (!file.good())
    throw std::runtime_error(std::format("Failed to open repository config file: {}", config_path.string()));
  auto config = nlohmann::json::parse(file).get<RepositoryConfig>();
  if (verbose) println("Loading packages from repository config file: {}... ", config_path.string());
  return load_repositories(config, cache_dir, flush_if_needed, verbose);
}

void PackageLoader::flush_buffer(bool update_if_exists, bool verbose) const {
  if (verbose) print("Flushing to disk... ");
  auto time = xpg::measure_time<std::chrono::milliseconds>([=, this] { graph_.flush_buffer(update_if_exists); });
  if (verbose) {
    println("Done. ({:.3f} s)", time.count() / 1000.0);
    println("  Total: {} packages, {} versions, {} dependencies (disk)",
            graph_.package_count(), graph_.version_count(), graph_.dependency_count());
  }
}

void PackageLoader::build_cache(bool verbose) const {
  if (verbose) print("Building cache on GPU... ");
  graph_.clear_cache();
  auto before = getGpuMemInfo();
  auto time = xpg::measure_time<std::chrono::milliseconds>([this] { graph_.build_cache(); });
  auto after = getGpuMemInfo();
  auto used = before.free_bytes - after.free_bytes;
  if (verbose) {
    println("Done. ({:.3f} s)", time.count() / 1000.0);
    println("  GPU memory: {:.3f} GiB used, {:.3f} GiB free, {:.3f} GiB total",
            used / GiBd, after.free_bytes / GiBd, after.total_bytes / GiBd);
  }
}

void PackageLoader::compact(bool verbose) const {
  if (verbose) print("Compact storage... ");
  auto time = xpg::measure_time<std::chrono::milliseconds>([this] { graph_.compact(); });
  if (verbose) {
    println("Done. ({:.3f} s)", time.count() / 1000.0);
    println("  Total: {} packages, {} versions, {} dependencies (disk)",
            graph_.package_count(), graph_.version_count(), graph_.dependency_count());
  }
}

} // namespace xpg
