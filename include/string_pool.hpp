#pragma once
#include <cstddef>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include "config.hpp"
#include "mmap_vector.hpp"

template <class Char, class Len, class Traits>
class basic_string_pool;

template <class Char, class Len, class Traits>
class basic_string_pool_iterator {
public:
  using pool_type = basic_string_pool<Char, Len, Traits>;
  using view_type = pool_type::view_type;
  using offset_type = pool_type::offset_type;
  using length_type = pool_type::length_type;
  using value_type = view_type;
  using pointer = void;
  using reference = value_type;
  using difference_type = pool_type::difference_type;
  using iterator_category = std::forward_iterator_tag;

  basic_string_pool_iterator(const pool_type &pool, offset_type offset = 0) noexcept : pool_(pool), offset_(offset) {}

  reference operator*() const noexcept { return view(); }

  basic_string_pool_iterator &operator++() noexcept;
  basic_string_pool_iterator operator++(int) noexcept;

  bool operator==(const basic_string_pool_iterator &other) const noexcept { return offset_ == other.offset_; }
  bool operator!=(const basic_string_pool_iterator &other) const noexcept { return !(*this == other); }

  view_type view() const noexcept { return pool_.get(offset_); }
  offset_type offset() const noexcept { return offset_; }
  length_type length() const noexcept { return view().size(); }

private:
  const pool_type &pool_;
  offset_type offset_;
};

template <class Char, class Len = std::size_t, class Traits = std::char_traits<Char>>
class basic_string_pool {
  friend class basic_string_pool_iterator<Char, Len, Traits>;

public:
  static constexpr bool use_length = !std::is_void_v<Len>;
  static constexpr bool no_length = !use_length;

  using char_type = Char;
  using traits_type = Traits;
  using container_type = mmap_vector<std::byte>;
  using view_type = std::basic_string_view<Char, Traits>;
  using size_type = container_type::size_type;
  using offset_type = size_type;
  using length_type = std::conditional_t<use_length, Len, size_type>;
  using difference_type = container_type::difference_type;
  using path_type = container_type::path_type;
  using iterator = std::conditional_t<use_length, basic_string_pool_iterator<Char, Len, Traits>, void>;
  using const_iterator = iterator;

  struct pooled_string {
    offset_type offset;
    length_type length;
  };

  basic_string_pool(size_type chunk_bytes = kDefaultChunkBytes) noexcept : pool_(chunk_bytes) {}
  basic_string_pool(const path_type &path, open_mode mode = open_mode::kLoadOrCreate,
                    size_type chunk_bytes = kDefaultChunkBytes) noexcept;

  basic_string_pool(const basic_string_pool &) = delete;
  basic_string_pool &operator=(const basic_string_pool &) = delete;

  basic_string_pool(basic_string_pool &&) noexcept = default;
  basic_string_pool &operator=(basic_string_pool &&) noexcept = default;

  ~basic_string_pool() = default;

  bool load(const path_type &path) noexcept;
  bool create(const path_type &path) noexcept;

  open_code open(const path_type &path, open_mode mode = open_mode::kLoadOrCreate) noexcept;
  void close() { pool_.close(); }
  void sync() { pool_.sync(); }

  bool is_open() const noexcept { return pool_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  size_type chunk_bytes() const noexcept { return pool_.chunk_bytes(); }
  void set_chunk_bytes(size_type chunk_bytes) noexcept { pool_.set_chunk_bytes(chunk_bytes); }

  static constexpr size_type char_size() noexcept { return sizeof(char_type); }
  static constexpr size_type length_size() noexcept { return use_length ? sizeof(length_type) : 0; }

  size_type size_bytes() const noexcept { return pool_.size() - header_size(); }
  size_type capacity_bytes() const noexcept { return pool_.capacity() - header_size(); }

  bool empty() const noexcept { return size_bytes() == 0; }

  iterator begin() noexcept requires use_length { return iterator(*this); }
  const_iterator begin() const noexcept requires use_length { return const_iterator(*this); }
  const_iterator cbegin() const noexcept requires use_length { return const_iterator(*this); }

  iterator end() noexcept requires use_length { return iterator(*this, size_bytes()); }
  const_iterator end() const noexcept requires use_length { return const_iterator(*this, size_bytes()); }
  const_iterator cend() const noexcept requires use_length { return const_iterator(*this, size_bytes()); }

  void reserve_bytes(size_type capacity) { pool_.reserve(header_size() + capacity); }
  void clear() { pool_.resize(header_size()); }

  view_type get(offset_type offset) const noexcept requires use_length;
  view_type get(offset_type offset, length_type length) const noexcept requires no_length;
  view_type get(pooled_string str) const noexcept requires no_length { return get(str.offset, str.length); }

  offset_type add(view_type str) requires use_length;
  pooled_string add(view_type str) requires no_length;

  offset_type append(view_type str) requires use_length { return add(str); }
  pooled_string append(view_type str) requires no_length { return add(str); }

private:
  container_type pool_;

  struct header_type {
    size_type magic;
    size_type char_size;
    size_type length_size;
  };

  struct use_length_entry {
    length_type length;
    char_type data[];
  };

  struct no_length_entry {
    char_type data[];
  };

  using entry_type = std::conditional_t<use_length, use_length_entry, no_length_entry>;

  static constexpr size_type header_magic = 0x4c4f4f5049525453; // "STRIPOOL"

  static constexpr size_type header_size() noexcept { return sizeof(header_type); }
  static constexpr size_type entry_size(length_type length) noexcept;
  static constexpr size_type entry_align() noexcept { return alignof(entry_type); }

  header_type &header() noexcept { return *reinterpret_cast<header_type *>(pool_.data()); }
  const header_type &header() const noexcept { return *reinterpret_cast<const header_type *>(pool_.data()); }

  bool validate_header() const noexcept;

  entry_type &entry(offset_type offset) noexcept;
  const entry_type &entry(offset_type offset) const noexcept;
};

template <class Char, class Len = std::size_t, class Traits = std::char_traits<Char>>
struct basic_pooled_string_hash {
  using is_transparent = void;
  using pool_type = basic_string_pool<Char, Len, Traits>;
  using view_type = pool_type::view_type;
  using offset_type = pool_type::offset_type;
  using pooled_string = pool_type::pooled_string;

  static constexpr bool use_length = pool_type::use_length;
  static constexpr bool no_length = pool_type::no_length;

  basic_pooled_string_hash(const pool_type &pool) noexcept : pool_(pool) {}

  std::size_t operator()(offset_type key) const noexcept requires use_length;
  std::size_t operator()(pooled_string key) const noexcept requires no_length;
  std::size_t operator()(view_type key) const noexcept { return std::hash<view_type>()(key); }

private:
  const pool_type &pool_;
};

template <class Char, class Len = std::size_t, class Traits = std::char_traits<Char>>
struct basic_pooled_string_equal_to {
  using is_transparent = void;
  using pool_type = basic_string_pool<Char, Len, Traits>;
  using view_type = pool_type::view_type;
  using offset_type = pool_type::offset_type;
  using pooled_string = pool_type::pooled_string;

  static constexpr bool use_length = pool_type::use_length;
  static constexpr bool no_length = pool_type::no_length;

  basic_pooled_string_equal_to(const pool_type &pool) noexcept : pool_(pool) {}

  bool operator()(offset_type l, offset_type r) const noexcept requires use_length;
  bool operator()(offset_type l, view_type r) const noexcept requires use_length { return pool_.get(l) == r; }
  bool operator()(view_type l, offset_type r) const noexcept requires use_length { return l == pool_.get(r); }
  bool operator()(pooled_string l, pooled_string r) const noexcept requires no_length;
  bool operator()(pooled_string l, view_type r) const noexcept requires no_length { return pool_.get(l) == r; }
  bool operator()(view_type l, pooled_string r) const noexcept requires no_length { return l == pool_.get(r); }
  bool operator()(view_type l, view_type r) const noexcept { return l == r; }

private:
  const pool_type &pool_;
};

template <class T, class Char, class Len = std::size_t, class Traits = std::char_traits<Char>>
using basic_pooled_string_map = std::unordered_map<
  std::conditional_t<basic_string_pool<Char, Len, Traits>::use_length,
                     typename basic_string_pool<Char, Len, Traits>::offset_type,
                     typename basic_string_pool<Char, Len, Traits>::pooled_string>, T,
  basic_pooled_string_hash<Char, Len, Traits>, basic_pooled_string_equal_to<Char, Len, Traits>>;

template <class Char, class Len = std::size_t, class Traits = std::char_traits<Char>>
using basic_pooled_string_set = std::unordered_set<
  std::conditional_t<basic_string_pool<Char, Len, Traits>::use_length,
                     typename basic_string_pool<Char, Len, Traits>::offset_type,
                     typename basic_string_pool<Char, Len, Traits>::pooled_string>,
  basic_pooled_string_hash<Char, Len, Traits>, basic_pooled_string_equal_to<Char, Len, Traits>>;

template <class Len = std::size_t>
using string_pool = basic_string_pool<char, Len>;

template <class Len = std::size_t>
using wstring_pool = basic_string_pool<wchar_t, Len>;

template <class Len = std::size_t>
using u16string_pool = basic_string_pool<char16_t, Len>;

template <class Len = std::size_t>
using u32string_pool = basic_string_pool<char32_t, Len>;

template <class T, class Len = std::size_t>
using pooled_string_map = basic_pooled_string_map<T, char, Len>;

template <class T, class Len = std::size_t>
using pooled_wstring_map = basic_pooled_string_map<T, wchar_t, Len>;

template <class T, class Len = std::size_t>
using pooled_u16string_map = basic_pooled_string_map<T, char16_t, Len>;

template <class T, class Len = std::size_t>
using pooled_u32string_map = basic_pooled_string_map<T, char32_t, Len>;

template <class Len = std::size_t>
using pooled_string_set = basic_pooled_string_set<char, Len>;

template <class Len = std::size_t>
using pooled_wstring_set = basic_pooled_string_set<wchar_t, Len>;

template <class Len = std::size_t>
using pooled_u16string_set = basic_pooled_string_set<char16_t, Len>;

template <class Len = std::size_t>
using pooled_u32string_set = basic_pooled_string_set<char32_t, Len>;

#include "details/string_pool.ipp"
