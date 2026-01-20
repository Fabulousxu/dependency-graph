#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include "disk_vector.hpp"

struct StringView {
  using OffsetType = std::uint32_t;
  using LengthType = std::uint8_t;
  OffsetType offset;
  LengthType length;
  StringView(std::size_t offset, std::size_t length) : offset(offset), length(length) {}
};

class StringPool {
public:
  StringPool(std::string_view path, std::size_t chunk_bytes = kDefaultChunkBytes) : pool_{path, chunk_bytes} {}
  ~StringPool() = default;

  std::string_view get(StringView sv) const noexcept { return {pool_.data() + sv.offset, sv.length}; }

  StringView add(std::string_view sv) {
    pool_.append(sv.begin(), sv.end());
    return {pool_.size() - sv.size(), sv.size()};
  }

private:
  DiskVector<char> pool_;
};

struct StringViewHash {
  using is_transparent = void;
  StringPool &string_pool_;
  std::size_t operator()(StringView key) const noexcept { return std::hash<std::string_view>{}(string_pool_.get(key)); }
  std::size_t operator()(std::string_view key) const noexcept { return std::hash<std::string_view>{}(key); }
};

struct StringViewEqual {
  using is_transparent = void;
  StringPool &string_pool_;
  bool operator()(StringView l, std::string_view r) const noexcept { return string_pool_.get(l) == r; }
  bool operator()(std::string_view l, StringView r) const noexcept { return l == string_pool_.get(r); }

  bool operator()(StringView l, StringView r) const noexcept {
    if (l.offset == r.offset && l.length == r.length) return true;
    return string_pool_.get(l) == string_pool_.get(r);
  }
};

template <class T>
using StringViewMap = std::unordered_map<StringView, T, StringViewHash, StringViewEqual>;
