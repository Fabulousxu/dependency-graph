#pragma once
#include <concepts>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include "config.hpp"
#include "string_map.hpp"
#include "string_pool.hpp"

template <class Char, class Traits = std::char_traits<Char>>
class basic_symbol_table_iterator {
public:
  using char_type = Char;
  using traits_type = Traits;
  using value_type = std::basic_string_view<char_type, traits_type>;
  using pointer = void;
  using reference = value_type;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::bidirectional_iterator_tag;
  using pool_type = basic_string_pool<char_type, true, traits_type>;
  using handle_iterator = std::vector<string_handle>::iterator;

  basic_symbol_table_iterator(handle_iterator iterator, const pool_type &symbols) noexcept;

  value_type operator*() const noexcept { return symbols_.get(*iterator_); }

  basic_symbol_table_iterator &operator++() noexcept;
  basic_symbol_table_iterator operator++(int) noexcept;
  basic_symbol_table_iterator &operator--() noexcept;
  basic_symbol_table_iterator operator--(int) noexcept;

  bool operator==(const basic_symbol_table_iterator &other) const noexcept { return iterator_ == other.iterator_; }
  bool operator!=(const basic_symbol_table_iterator &other) const noexcept { return iterator_ != other.iterator_; }

  value_type view() const noexcept { return **this; }

private:
  handle_iterator iterator_;
  const pool_type &symbols_;
};

template <class Id, class Char, class Traits = std::char_traits<Char>>
class basic_symbol_table {
  static_assert(std::integral<Id> || std::is_enum_v<Id>);

public:
  using id_type = Id;
  using char_type = Char;
  using traits_type = Traits;
  using iterator = basic_symbol_table_iterator<char_type, traits_type>;
  using const_iterator = iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using view_type = std::basic_string_view<char_type, traits_type>;
  using size_type = std::size_t;
  using path_type = std::filesystem::path;

  basic_symbol_table(size_type chunk_bytes = kSmallChunkBytes) noexcept;
  basic_symbol_table(const path_type &path, open_mode mode = open_mode::kLoadOrCreate,
                     std::initializer_list<view_type> symbols = {}, size_type chunk_bytes = kSmallChunkBytes) noexcept;

  open_code open(const path_type &path, open_mode mode = open_mode::kLoadOrCreate,
                 std::initializer_list<view_type> symbols = {}) noexcept;
  void close();
  void sync() { symbols_.sync(); }

  bool is_open() const noexcept { return symbols_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  size_type chunk_bytes() const noexcept { return symbols_.chunk_bytes(); }
  void set_chunk_bytes(size_type chunk_bytes) noexcept { symbols_.set_chunk_bytes(chunk_bytes); }

  size_type size() const noexcept { return id_to_symbol_.size(); }
  size_type symbol_count() const noexcept { return size(); }

  iterator begin() noexcept { return iterator(id_to_symbol_.begin(), symbols_); }
  const_iterator begin() const noexcept { return const_iterator(id_to_symbol_.begin(), symbols_); }
  const_iterator cbegin() const noexcept { return const_iterator(id_to_symbol_.begin(), symbols_); }

  iterator end() noexcept { return iterator(id_to_symbol_.end(), symbols_); }
  const_iterator end() const noexcept { return const_iterator(id_to_symbol_.end(), symbols_); }
  const_iterator cend() const noexcept { return const_iterator(id_to_symbol_.end(), symbols_); }

  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }

  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

  view_type get(id_type id) const noexcept { return symbols_.get(id_to_symbol_[id]); }
  view_type at(id_type id) const noexcept { return get(id); }

  std::optional<id_type> id(view_type symbol) const noexcept;
  std::optional<id_type> index(view_type symbol) const noexcept { return id(symbol); }

  view_type operator[](id_type id) const noexcept { return get(id); }
  std::optional<id_type> operator[](view_type symbol) const noexcept { return id(symbol); }

  id_type add(view_type symbol);
  id_type append(view_type symbol) { return add(symbol); }

private:
  basic_string_pool<char_type, true, traits_type> symbols_;
  std::vector<string_handle> id_to_symbol_;
  basic_string_handle_map<id_type, char_type, true, traits_type> symbol_to_id_;
};

template <class Id>
using symbol_table = basic_symbol_table<Id, char>;

#include "details/symbol_table.ipp"
