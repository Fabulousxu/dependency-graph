#pragma once
#include <iterator>
#include <string>
#include <string_view>
#include "config.hpp"
#include "disk_vector.hpp"

struct string_handle {
  using offset_type = string_handle_offset_t;
  using length_type = string_handle_length_t;

  offset_type offset;
  length_type length;
};

template <class Char, class Traits = std::char_traits<Char>>
class basic_string_pool_iterator {
public:
  using char_type = Char;
  using traits_type = Traits;
  using value_type = std::basic_string_view<char_type, traits_type>;
  using pointer = void;
  using reference = value_type;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::bidirectional_iterator_tag;
  using handle_type = string_handle;

  static constexpr char_type null = static_cast<char_type>(0);

  basic_string_pool_iterator(const char_type *data, const char_type *begin = nullptr) noexcept;
  ~basic_string_pool_iterator() noexcept = default;

  value_type operator*() const noexcept { return value_type(data_, traits_type::length(data_)); }

  basic_string_pool_iterator &operator++() noexcept;
  basic_string_pool_iterator operator++(int) noexcept;
  basic_string_pool_iterator &operator--() noexcept;
  basic_string_pool_iterator operator--(int) noexcept;

  bool operator==(const basic_string_pool_iterator &other) const noexcept { return data_ == other.data_; }
  bool operator!=(const basic_string_pool_iterator &other) const noexcept { return data_ != other.data_; }

  handle_type handle() const noexcept;
  value_type view() const noexcept { return **this; }

private:
  const char_type *data_;
  const char_type *begin_;
};

template <class Char, bool NullTerminated = false, class Traits = std::char_traits<Char>>
class basic_string_pool_base {
public:
  using char_type = Char;
  using value_type = char_type;
  using traits_type = Traits;
  using handle_type = string_handle;
  using view_type = std::basic_string_view<char_type, traits_type>;
  using size_type = std::size_t;
  using path_type = std::filesystem::path;

  static constexpr char_type null = static_cast<char_type>(0);

  basic_string_pool_base(size_type chunk_bytes = kDefaultChunkBytes) noexcept : pool_(chunk_bytes) {}
  basic_string_pool_base(const path_type &path, open_mode mode = open_mode::kLoadOrCreate,
                         size_type chunk_bytes = kDefaultChunkBytes) noexcept;
  ~basic_string_pool_base() = default;

  open_code open(const path_type &path, open_mode mode = open_mode::kLoadOrCreate) noexcept;
  void close() { pool_.close(); }
  void sync() { pool_.sync(); }

  bool is_open() const noexcept { return pool_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  size_type chunk_bytes() const noexcept { return pool_.chunk_bytes(); }
  void set_chunk_bytes(size_type chunk_bytes) noexcept { pool_.set_chunk_bytes(chunk_bytes); }

  size_type size() const noexcept { return pool_.size(); }
  size_type capacity() const noexcept { return pool_.capacity(); }

  void reserve(size_type capacity) { pool_.reserve(capacity); }
  void resize(size_type size) { pool_.resize(size); }
  void clear() { pool_.clear(); }

  view_type get(handle_type::offset_type offset, handle_type::length_type length) const noexcept;
  view_type get(handle_type handle) const noexcept { return get(handle.offset, handle.length); }

  handle_type add(view_type view);
  handle_type append(view_type view) { return add(view); }

  basic_string_pool_base &operator+=(view_type view);

protected:
  disk_vector<char_type> pool_;
};

template <class Char, bool NullTerminated = false, class Traits = std::char_traits<Char>>
class basic_string_pool : public basic_string_pool_base<Char, NullTerminated, Traits> {
  using basic_string_pool_base<Char, NullTerminated, Traits>::basic_string_pool_base;
};

template <class Char, class Traits>
class basic_string_pool<Char, true, Traits> : public basic_string_pool_base<Char, true, Traits> {
  using base_type = basic_string_pool_base<Char, true, Traits>;
  using typename base_type::char_type;
  using typename base_type::traits_type;
  using base_type::basic_string_pool_base;
  using base_type::pool_;

public:
  using iterator = basic_string_pool_iterator<char_type, traits_type>;
  using const_iterator = iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  iterator begin() noexcept { return iterator(pool_.begin()); }
  const_iterator begin() const noexcept { return const_iterator(pool_.begin()); }
  const_iterator cbegin() const noexcept { return const_iterator(pool_.begin()); }

  iterator end() noexcept { return iterator(pool_.end(), pool_.begin()); }
  const_iterator end() const noexcept { return const_iterator(pool_.end(), pool_.begin()); }
  const_iterator cend() const noexcept { return const_iterator(pool_.end(), pool_.begin()); }

  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }

  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }
};

template <bool NullTerminated = false>
using string_pool = basic_string_pool<char, NullTerminated>;

#include "details/string_pool.ipp"
