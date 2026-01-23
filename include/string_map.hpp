#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include "string_pool.hpp"

template <class Char, class Traits = std::char_traits<Char>>
struct BasicStringHash {
  using is_transparent = void;
  using view_type = std::basic_string_view<Char, Traits>;
  std::size_t operator()(view_type key) const noexcept { return std::hash<view_type>{}(key); }
};

template <class Char, class Traits = std::char_traits<Char>>
struct BasicStringEqual {
  using is_transparent = void;
  using view_type = std::basic_string_view<Char, Traits>;
  bool operator()(view_type l, view_type r) const noexcept { return l == r; }
};

template <class Char, class T, class Traits = std::char_traits<Char>, class Alloc = std::allocator<Char>>
using BasicStringMap = std::unordered_map<
  std::basic_string<Char, Traits, Alloc>, T, BasicStringHash<Char, Traits>, BasicStringEqual<Char, Traits>>;

template <class Char, class Traits = std::char_traits<Char>, class Alloc = std::allocator<Char>>
using BasicStringSet = std::unordered_set<
  std::basic_string<Char, Traits, Alloc>, BasicStringHash<Char, Traits>, BasicStringEqual<Char, Traits>>;

template <class T>
using StringMap = BasicStringMap<char, T>;

using StringSet = BasicStringSet<char>;


template <class Char, bool NullTerminated, class Traits = std::char_traits<Char>>
struct BasicStringHandleHash {
  using is_transparent = void;
  using pool_type = BasicStringPool<Char, NullTerminated, Traits>;
  using handle_type = pool_type::handle_type;
  using view_type = pool_type::view_type;
  const pool_type &pool_;

  BasicStringHandleHash(const pool_type &pool) : pool_(pool) {}
  std::size_t operator()(StringHandle key) const noexcept { return std::hash<view_type>{}(pool_.get(key)); }
  std::size_t operator()(view_type key) const noexcept { return std::hash<view_type>{}(key); }
};

template <class Char, bool NullTerminated, class Traits = std::char_traits<Char>>
struct BasicStringHandleEqual {
  using is_transparent = void;
  using pool_type = BasicStringPool<Char, NullTerminated, Traits>;
  using handle_type = pool_type::handle_type;
  using view_type = pool_type::view_type;
  const pool_type &pool_;

  BasicStringHandleEqual(const pool_type &pool) : pool_(pool) {}
  bool operator()(handle_type l, handle_type r) const noexcept;
  bool operator()(handle_type l, view_type r) const noexcept { return pool_.get(l) == r; }
  bool operator()(view_type l, handle_type r) const noexcept { return l == pool_.get(r); }
  bool operator()(view_type l, view_type r) const noexcept { return l == r; }
};

template <class Char, bool NullTerminated, class Traits>
bool BasicStringHandleEqual<Char, NullTerminated, Traits>::operator()(handle_type l, handle_type r) const noexcept {
  if (l.offset == r.offset && l.length == r.length) return true;
  return pool_.get(l) == pool_.get(r);
}

template <class Char, class T, bool NullTerminated = false, class Traits = std::char_traits<Char>>
using BasicStringHandleMap = std::unordered_map<
  StringHandle, T, BasicStringHandleHash<Char, NullTerminated, Traits>,
  BasicStringHandleEqual<Char, NullTerminated, Traits>>;

template <class Char, bool NullTerminated = false, class Traits = std::char_traits<Char>>
using BasicStringHandleSet = std::unordered_set<
  StringHandle, BasicStringHandleHash<Char, NullTerminated, Traits>,
  BasicStringHandleEqual<Char, NullTerminated, Traits>>;

template <bool NullTerminated = false>
using StringHandleHash = BasicStringHandleHash<char, NullTerminated>;

template <bool NullTerminated = false>
using StringHandleEqual = BasicStringHandleEqual<char, NullTerminated>;

template <class T, bool NullTerminated = false>
using StringHandleMap = BasicStringHandleMap<char, T, NullTerminated>;

template <bool NullTerminated = false>
using StringHandleSet = BasicStringHandleSet<char, NullTerminated>;
