#pragma once
#include <cstddef>
#include <cstdint>

using PackageId = std::uint32_t;
using VersionId = std::uint32_t;
using DependencyId = std::uint32_t;
using GroupId = std::uint8_t;
using ArchitectureType = std::uint8_t;
using DependencyType = std::uint8_t;
using string_handle_offset_t = std::uint32_t;
using string_handle_length_t = std::uint8_t;

class DependencyGraph;
class DiskGraph;
class BufferGraph;
class GpuGraph;
class PackageLoader;

enum open_mode : std::uint8_t { kLoad, kCreate, kLoadOrCreate };
enum open_code : std::uint8_t { kOpenFailed, kCreateSuccess, kLoadSuccess };

inline constexpr std::size_t KiB = 1024;
inline constexpr std::size_t MiB = 1024 * KiB;
inline constexpr std::size_t GiB = 1024 * MiB;
inline constexpr double KiB_d = 1024.0;
inline constexpr double MiB_d = 1024.0 * KiB_d;
inline constexpr double GiB_d = 1024.0 * MiB_d;
inline constexpr std::size_t kDefaultChunkBytes = 1 * MiB;
inline constexpr std::size_t kSmallChunkBytes = 256;
inline constexpr std::size_t kDefaultMemoryLimit = 1 * GiB;
inline constexpr std::size_t kDefaultMaxDeviceVectorBytes = 64 * MiB;
