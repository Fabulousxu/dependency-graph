#pragma once
#include <concepts>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

class DependencyGraph;
class GpuDependencyGraph;
class PackageLoader;

using PackageId = std::uint32_t;
using VersionId = std::uint32_t;
using DependencyId = std::uint32_t;
using GroupId = std::uint8_t;
using ArchitectureType = std::uint8_t;
using DependencyType = std::uint8_t;
using DegreeType = std::uint16_t;
using VisitedMark = std::uint32_t;

struct DependencyItem {
  std::string_view name;
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
  return l.name == r.name && l.dependency_type == r.dependency_type
    && l.version_constraint == r.version_constraint && l.architecture_constraint == r.architecture_constraint;
}

template <>
struct std::hash<DependencyItem> {
  std::size_t operator()(const DependencyItem &item) const noexcept {
    auto h = std::hash<std::string_view>{};
    auto seed = h(item.name);
    seed ^= h(item.dependency_type) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= h(item.version_constraint) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return h(item.architecture_constraint) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
  }
};

using json = nlohmann::ordered_json;

inline void to_json(json &j, const DependencyItem &item) noexcept {
  j["package_name"] = item.name;
  j["type"] = item.dependency_type;
  j["version_constraint"] = item.version_constraint;
  j["architecture_constraint"] = item.architecture_constraint;
}

inline void to_json(json &j, const DependencyLevel &dlevel) noexcept {
  j["direct_dependencies"] = dlevel.direct_dependencies;
  j["or_dependencies"] = dlevel.or_dependencies;
}

struct StringHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
};

struct StringEqual {
  using is_transparent = void;
  bool operator()(std::string_view l, std::string_view r) const noexcept { return l == r; }
};

template <class T>
using StringKeyHashMap = std::unordered_map<std::string, T, StringHash, StringEqual>;

template <std::integral Id>
class SymbolTable {
public:
  SymbolTable(std::initializer_list<std::string_view> symbols) noexcept { for (auto symbol : symbols) insert(symbol); }

  const std::vector<std::string> &symbols() const noexcept { return symbols_; }

  std::string_view symbol(Id id) const noexcept { return symbols_[id]; }

  std::optional<Id> id(std::string_view symbol) const noexcept {
    auto it = id_to_symbol_.find(symbol);
    return it != id_to_symbol_.end() ? std::optional<Id>{it->second} : std::nullopt;
  }

  Id insert(std::string_view symbol) noexcept {
    auto [it, succ] = id_to_symbol_.try_emplace(std::string{symbol}, static_cast<Id>(symbols_.size()));
    if (!succ) return it->second;
    symbols_.emplace_back(symbol);
    return it->second;
  }

private:
  std::vector<std::string> symbols_;
  StringKeyHashMap<Id> id_to_symbol_;
};
