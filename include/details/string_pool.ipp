#pragma once
#include "util.hpp"

template <class Char, class Len, class Traits>
auto basic_string_pool_iterator<Char, Len, Traits>::operator++() noexcept -> basic_string_pool_iterator & {
  offset_ += pool_type::entry_size(length());
  return *this;
}

template <class Char, class Len, class Traits>
auto basic_string_pool_iterator<Char, Len, Traits>::operator++(int) noexcept -> basic_string_pool_iterator {
  basic_string_pool_iterator temp = *this;
  ++*this;
  return temp;
}


template <class Char, class Len, class Traits>
basic_string_pool<Char, Len, Traits>::basic_string_pool(const path_type &path, open_mode mode, size_type chunk_bytes)
  noexcept : basic_string_pool(chunk_bytes) {
  open(path, mode);
}

template <class Char, class Len, class Traits>
bool basic_string_pool<Char, Len, Traits>::validate_header() const noexcept {
  return header().magic == header_magic && header().char_size == char_size() && header().length_size == length_size();
}

template <class Char, class Len, class Traits>
bool basic_string_pool<Char, Len, Traits>::load(const path_type &path) noexcept {
  if (!pool_.load(path)) return false;
  if (pool_.size() >= header_size() && validate_header()) return true;
  pool_.close();
  return false;
}

template <class Char, class Len, class Traits>
bool basic_string_pool<Char, Len, Traits>::create(const path_type &path) noexcept {
  if (!pool_.create(path)) return false;
  pool_.resize(header_size());
  header() = {
    .magic = header_magic,
    .char_size = char_size(),
    .length_size = length_size(),
  };
  return true;
}

template <class Char, class Len, class Traits>
open_code basic_string_pool<Char, Len, Traits>::open(const path_type &path, open_mode mode) noexcept {
  switch (mode) {
  case open_mode::kLoad:
    if (load(path)) return open_code::kLoadSuccess;
    return open_code::kOpenFailed;
  case open_mode::kCreate:
    if (create(path)) return open_code::kCreateSuccess;
    return open_code::kOpenFailed;
  case open_mode::kLoadOrCreate:
    if (load(path)) return open_code::kLoadSuccess;
    if (create(path)) return open_code::kCreateSuccess;
    return open_code::kOpenFailed;
  default:
    return open_code::kOpenFailed;
  }
}

template <class Char, class Len, class Traits>
constexpr auto basic_string_pool<Char, Len, Traits>::entry_size(length_type length) noexcept -> size_type {
  if constexpr (use_length) return sizeof(entry_type) + alignup(length * char_size(), entry_align());
  else return length * char_size();
}

template <class Char, class Len, class Traits>
auto basic_string_pool<Char, Len, Traits>::entry(offset_type offset) noexcept -> entry_type & {
  return *reinterpret_cast<entry_type *>(pool_.data() + header_size() + offset);
}

template <class Char, class Len, class Traits>
auto basic_string_pool<Char, Len, Traits>::entry(offset_type offset) const noexcept -> const entry_type & {
  return *reinterpret_cast<const entry_type *>(pool_.data() + header_size() + offset);
}

template <class Char, class Len, class Traits>
auto basic_string_pool<Char, Len, Traits>::get(offset_type offset) const noexcept -> view_type requires use_length {
  return view_type(entry(offset).data, entry(offset).length);
}

template <class Char, class Len, class Traits>
auto basic_string_pool<Char, Len, Traits>::get(offset_type offset, length_type length) const noexcept
  -> view_type requires no_length {
  return view_type(entry(offset).data, length);
}

template <class Char, class Len, class Traits>
auto basic_string_pool<Char, Len, Traits>::add(view_type str) -> offset_type requires use_length {
  auto offset = static_cast<offset_type>(size_bytes());
  auto length = static_cast<length_type>(str.size());
  pool_.resize(header_size() + size_bytes() + entry_size(length));
  entry(offset).length = length;
  traits_type::copy(entry(offset).data, str.data(), length);
  return offset;
}

template <class Char, class Len, class Traits>
auto basic_string_pool<Char, Len, Traits>::add(view_type str) -> pooled_string requires no_length {
  auto offset = static_cast<offset_type>(size_bytes());
  auto length = static_cast<length_type>(str.size());
  pool_.resize(header_size() + size_bytes() + entry_size(length));
  traits_type::copy(entry(offset).data, str.data(), length);
  return {
    .offset = offset,
    .length = length,
  };
}


template <class Char, class Len, class Traits>
std::size_t basic_pooled_string_hash<Char, Len, Traits>::operator()(offset_type key) const noexcept
  requires use_length {
  return std::hash<view_type>()(pool_.get(key));
}

template <class Char, class Len, class Traits>
std::size_t basic_pooled_string_hash<Char, Len, Traits>::operator()(pooled_string key) const noexcept
  requires no_length {
  return std::hash<view_type>()(pool_.get(key));
}

template <class Char, class Len, class Traits>
bool basic_pooled_string_equal_to<Char, Len, Traits>::operator()(offset_type l, offset_type r) const noexcept
  requires use_length {
  return l == r || pool_.get(l) == pool_.get(r);
}

template <class Char, class Len, class Traits>
bool basic_pooled_string_equal_to<Char, Len, Traits>::operator()(pooled_string l, pooled_string r) const noexcept
  requires no_length {
  return (l.offset == r.offset && l.length == r.length) || pool_.get(l) == pool_.get(r);
}
