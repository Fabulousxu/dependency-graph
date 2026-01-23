#pragma once
#include <cstdint>
#include <cstddef>

using PackageId = std::uint32_t;
using VersionId = std::uint32_t;
using DependencyId = std::uint32_t;
using GroupId = std::uint8_t;
using ArchitectureType = std::uint8_t;
using DependencyType = std::uint8_t;
using StringHandleOffsetType = std::uint32_t;
using StringHandleLengthType = std::uint8_t;

enum OpenMode {
  kLoad,
  kCreate,
  kLoadOrCreate
};

enum OpenCode {
  kOpenFailed,
  kLoadSuccess,
  kCreateSuccess,
};

constexpr std::size_t kDefaultChunkBytes = 1 * 1024 * 1024;
constexpr std::size_t kSmallChunkBytes = 128;
constexpr std::size_t kDefaultMemoryLimit = 1024 * 1024 * 1024;
