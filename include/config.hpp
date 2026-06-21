#pragma once
#include <cstddef>
#include <cstdint>

namespace xpg {

#ifdef __CUDACC__
#define HOST_DEVICE __host__ __device__
#else
#define HOST_DEVICE
#endif

class StorageGraph;
class BufferGraph;
class CacheGraph;
class XPackageGraph;
class PackageLoader;

using PackageId = std::uint32_t;
using VersionId = std::uint32_t;
using DependencyId = std::uint32_t;
using ArchitectureId = std::uint8_t;
using DependencyType = std::uint8_t;
using GroupId = std::uint8_t;
using StringOffset = std::uint32_t;
using StringLength = std::uint8_t;
using VersionRangeId = std::uint32_t;
using VersionCount = std::uint16_t;
using DependencyCount = std::uint16_t;

inline constexpr std::size_t KiB = 1024;
inline constexpr std::size_t MiB = 1024 * KiB;
inline constexpr std::size_t GiB = 1024 * MiB;
inline constexpr std::size_t TiB = 1024 * GiB;
inline constexpr double KiBd = 1024;
inline constexpr double MiBd = 1024 * KiBd;
inline constexpr double GiBd = 1024 * MiBd;
inline constexpr double TiBd = 1024 * GiBd;
inline constexpr ArchitectureId kNullArchitecture = 0;
inline constexpr ArchitectureId kAnyArchitecture = 1;
inline constexpr ArchitectureId kAllArchitecture = 2;

} // namespace xpg
