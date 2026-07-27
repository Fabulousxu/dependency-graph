#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include "device_string_view.hpp"
#include "utils.hpp"

namespace xpg {

using PackageId = std::uint32_t;
using VersionId = std::uint32_t;
using VersionRangeId = std::uint32_t;
using VersionCount = std::uint16_t;
using DependencyId = std::uint32_t;
using DependencyGroupId = std::uint8_t;
using DependencyCount = std::uint16_t;
using StringId = std::uint32_t;
using StringLength = std::uint8_t;

enum class ArchitectureType : std::uint8_t { kNull, kAll, kAny, kNoarch };
inline constexpr std::string_view kArchitectureTypes[] = {"", "all", "any", "noarch"};

enum class DependencyType : std::uint8_t {
  kDepends, kPreDepends, kRecommends, kSuggests, kEnhances, kBreaks, kConflicts, kProvides, kReplaces, kBuiltUsing,
  kStaticBuiltUsing, kJavascriptBuiltUsing, kXCargoBuiltUsing, kRequires, kObsoletes, kSupplements
};

inline constexpr std::string_view kDependencyTypes[] = {
  "DEPENDS", "PRE_DEPENDS", "RECOMMENDS", "SUGGESTS", "ENHANCES", "BREAKS", "CONFLICTS", "PROVIDES", "REPLACES",
  "BUILT_USING", "STATIC_BUILT_USING", "JAVASCRIPT_BUILT_USING", "X_CARGO_BUILT_USING", "REQUIRES", "OBSOLETES",
  "SUPPLEMENTS"
};

inline const std::unordered_map<std::string_view, DependencyType> kDebDependencyTypes = {
  {"Depends", DependencyType::kDepends}, {"Pre-Depends", DependencyType::kPreDepends},
  {"Recommends", DependencyType::kRecommends}, {"Suggests", DependencyType::kSuggests},
  {"Enhances", DependencyType::kEnhances}, {"Breaks", DependencyType::kBreaks},
  {"Conflicts", DependencyType::kConflicts}, {"Provides", DependencyType::kProvides},
  {"Replaces", DependencyType::kReplaces}, {"Built-Using", DependencyType::kBuiltUsing},
  {"Static-Built-Using", DependencyType::kStaticBuiltUsing},
  {"Javascript-Built-Using", DependencyType::kJavascriptBuiltUsing},
  {"X-Cargo-Built-Using", DependencyType::kXCargoBuiltUsing}
};

inline const std::unordered_map<std::string_view, DependencyType> kRpmDependencyTypes = {
  {"rpm:requires", DependencyType::kRequires}, {"rpm:provides", DependencyType::kProvides},
  {"rpm:conflicts", DependencyType::kConflicts}, {"rpm:obsoletes", DependencyType::kObsoletes},
  {"rpm:recommends", DependencyType::kRecommends}, {"rpm:suggests", DependencyType::kSuggests},
  {"rpm:supplements", DependencyType::kSupplements}, {"rpm:enhances", DependencyType::kEnhances},
};

inline const std::unordered_map<std::string_view, std::string_view> kRpmFlags = {
  {"LT", "<"}, {"LE", "<="}, {"EQ", "="}, {"GE", ">="}, {"GT", ">"}
};

inline constexpr std::size_t kGrowthBytes = 1_MB;
inline constexpr std::size_t kFlushLimitBytes = -1_MB;
inline constexpr std::size_t kDeviceVectorBytes = 64_MB;
template <class T> inline constexpr std::size_t kDeviceVectorSize = kDeviceVectorBytes / sizeof(T);

HOST_DEVICE bool satisfy_architecture(ArchitectureType target, ArchitectureType constraint,
                                      ArchitectureType source) noexcept;
bool satisfy_architecture(std::string_view target, std::string_view constraint, std::string_view source) noexcept;
HOST_DEVICE bool satisfy_version(device_string_view target, device_string_view constraint) noexcept;

} // namespace xpg
