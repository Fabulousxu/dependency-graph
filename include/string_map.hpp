#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include "string_pool.hpp"

template <class Char, class Traits = std::char_traits<Char>>
struct basic_string_hash {
  using is_transparent = void;
  using view_type = std::basic_string_view<Char, Traits>;

  std::size_t operator()(view_type key) const noexcept { return std::hash<view_type>()(key); }
};

using string_hash = basic_string_hash<char>;


template <class Char, class Traits = std::char_traits<Char>>
struct basic_string_equal_to {
  using is_transparent = void;
  using view_type = std::basic_string_view<Char, Traits>;

  bool operator()(view_type l, view_type r) const noexcept { return l == r; }
};

using string_equal_to = basic_string_equal_to<char>;


template <class Char, bool NullTerminated = false, class Traits = std::char_traits<Char>>
struct basic_string_handle_hash {
  using is_transparent = void;
  using handle_type = string_handle;
  using view_type = std::basic_string_view<Char, Traits>;

  const basic_string_pool<Char, NullTerminated, Traits> &pool_;

  basic_string_handle_hash(const basic_string_pool<Char, NullTerminated, Traits> &pool) : pool_(pool) {}

  std::size_t operator()(handle_type key) const noexcept { return std::hash<view_type>{}(pool_.get(key)); }
  std::size_t operator()(view_type key) const noexcept { return std::hash<view_type>{}(key); }
};

template <bool NullTerminated = false>
using string_handle_hash = basic_string_handle_hash<char, NullTerminated>;


template <class Char, bool NullTerminated = false, class Traits = std::char_traits<Char>>
struct basic_string_handle_equal_to {
  using is_transparent = void;
  using handle_type = string_handle;
  using view_type = std::basic_string_view<Char, Traits>;

  const basic_string_pool<Char, NullTerminated, Traits> &pool_;

  basic_string_handle_equal_to(const basic_string_pool<Char, NullTerminated, Traits> &pool) : pool_(pool) {}

  bool operator()(handle_type l, handle_type r) const noexcept {
    if (l.offset == r.offset && l.length == r.length) return true;
    return pool_.get(l) == pool_.get(r);
  }

  bool operator()(handle_type l, view_type r) const noexcept { return pool_.get(l) == r; }
  bool operator()(view_type l, handle_type r) const noexcept { return l == pool_.get(r); }
  bool operator()(view_type l, view_type r) const noexcept { return l == r; }
};

template <bool NullTerminated = false>
using string_handle_equal_to = basic_string_handle_equal_to<char, NullTerminated>;


template <class T, class Char, class Traits = std::char_traits<Char>, class Alloc = std::allocator<Char>>
using basic_string_map = std::unordered_map<
  std::basic_string<Char, Traits, Alloc>, T, basic_string_hash<Char, Traits>, basic_string_equal_to<Char, Traits>>;

template <class T>
using string_map = basic_string_map<T, char>;


template <class Char, class Traits = std::char_traits<Char>, class Alloc = std::allocator<Char>>
using basic_string_set = std::unordered_set<
  std::basic_string<Char, Traits, Alloc>, basic_string_hash<Char, Traits>, basic_string_equal_to<Char, Traits>>;

using string_set = basic_string_set<char>;


template <class T, class Char, bool NullTerminated = false, class Traits = std::char_traits<Char>>
using basic_string_handle_map = std::unordered_map<
  string_handle, T, basic_string_handle_hash<Char, NullTerminated, Traits>,
  basic_string_handle_equal_to<Char, NullTerminated, Traits>>;

template <class T, bool NullTerminated = false>
using string_handle_map = basic_string_handle_map<T, char, NullTerminated>;


template <class Char, bool NullTerminated = false, class Traits = std::char_traits<Char>>
using basic_string_handle_set = std::unordered_set<
  string_handle, basic_string_handle_hash<Char, NullTerminated, Traits>,
  basic_string_handle_equal_to<Char, NullTerminated, Traits>>;

template <bool NullTerminated = false>
using string_handle_set = basic_string_handle_set<char, NullTerminated>;
