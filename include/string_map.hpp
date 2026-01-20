#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

struct StringHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view key) const noexcept { return std::hash<std::string_view>{}(key); }
};

struct StringEqual {
  using is_transparent = void;
  bool operator()(std::string_view l, std::string_view r) const noexcept { return l == r; }
};

template <class T>
using StringMap = std::unordered_map<std::string, T, StringHash, StringEqual>;
