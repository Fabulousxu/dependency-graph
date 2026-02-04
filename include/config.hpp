#pragma once
#include <cstddef>
#include <cstdint>

using PackageId = std::uint32_t;
using VersionId = std::uint32_t;
using DependencyId = std::uint32_t;
using GroupId = std::uint8_t;
using ArchitectureId = std::uint8_t;
using DependencyTypeId = std::uint8_t;
using cuda_size_t = unsigned long long;

class DependencyGraph;
class StorageGraph;
class BufferGraph;
class CacheGraph;
class PackageLoader;

enum class open_mode : std::uint8_t { kLoad, kCreate, kLoadOrCreate };
enum class open_code : std::uint8_t { kOpenFailed, kLoadSuccess, kCreateSuccess };

inline constexpr std::size_t KiB = 1024;
inline constexpr std::size_t MiB = 1024 * KiB;
inline constexpr std::size_t GiB = 1024 * MiB;
inline constexpr std::size_t TiB = 1024 * GiB;

inline constexpr double KiBd = 1024;
inline constexpr double MiBd = 1024 * KiBd;
inline constexpr double GiBd = 1024 * MiBd;
inline constexpr double TiBd = 1024 * GiBd;

inline constexpr std::size_t kDefaultChunkBytes = MiB;
inline constexpr std::size_t kSmallChunkBytes = KiB;
inline constexpr std::size_t kDefaultMemoryLimit = GiB;
inline constexpr std::size_t kMaxDeviceVectorBytes = 64 * MiB;
