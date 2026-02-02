#pragma once
#include <concepts>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <type_traits>
#include <vector>
#include "string_pool.hpp"

template <class Id, class Char, class Len, class Traits>
class basic_symbol_table;

template <class Id, class Char, class Len, class Traits>
class basic_symbol_table_iterator {
public:
  using table_type = basic_symbol_table<Id, Char, Len, Traits>;
  using id_type = table_type::id_type;
  using view_type = table_type::view_type;
  using value_type = view_type;
  using pointer = void;
  using reference = value_type;
  using difference_type = table_type::difference_type;
  using iterator_category = std::random_access_iterator_tag;

  basic_symbol_table_iterator(const table_type &table, id_type id = 0) noexcept : table_(table), id_(id) {}

  reference operator*() const noexcept { return view(); }

  basic_symbol_table_iterator &operator++() noexcept;
  basic_symbol_table_iterator operator++(int) noexcept;

  basic_symbol_table_iterator &operator--() noexcept;
  basic_symbol_table_iterator operator--(int) noexcept;

  basic_symbol_table_iterator &operator+=(difference_type n) noexcept;
  basic_symbol_table_iterator &operator-=(difference_type n) noexcept;

  basic_symbol_table_iterator operator+(difference_type n) const noexcept;
  basic_symbol_table_iterator operator-(difference_type n) const noexcept;

  difference_type operator-(const basic_symbol_table_iterator &other) const noexcept;

  bool operator==(const basic_symbol_table_iterator &other) const noexcept { return id_ == other.id_; }
  bool operator!=(const basic_symbol_table_iterator &other) const noexcept { return !(*this == other); }

  bool operator<(const basic_symbol_table_iterator &other) const noexcept { return id_ < other.id_; }
  bool operator<=(const basic_symbol_table_iterator &other) const noexcept { return id_ <= other.id_; }
  bool operator>(const basic_symbol_table_iterator &other) const noexcept { return id_ > other.id_; }
  bool operator>=(const basic_symbol_table_iterator &other) const noexcept { return id_ >= other.id_; }

  view_type view() const noexcept { return table_.get(id_); }
  id_type id() const noexcept { return id_; }
  id_type index() const noexcept { return id_; }

private:
  const table_type &table_;
  id_type id_;
};

template <class Id, class Char, class Len = std::size_t, class Traits = std::char_traits<Char>>
class basic_symbol_table {
  static_assert(std::integral<Id> || std::is_enum_v<Id>);
  static_assert(!std::is_void_v<Len>);

public:
  using id_type = Id;
  using char_type = Char;
  using traits_type = Traits;
  using pool_type = basic_string_pool<Char, Len, Traits>;
  using view_type = pool_type::view_type;
  using size_type = pool_type::size_type;
  using offset_type = pool_type::offset_type;
  using length_type = pool_type::length_type;
  using difference_type = pool_type::difference_type;
  using path_type = pool_type::path_type;
  using iterator = basic_symbol_table_iterator<Id, Char, Len, Traits>;
  using const_iterator = iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  basic_symbol_table(size_type chunk_bytes = kSmallChunkBytes) noexcept;
  basic_symbol_table(const path_type &path, open_mode mode = open_mode::kLoadOrCreate,
                     std::initializer_list<view_type> symbols = {}, size_type chunk_bytes = kSmallChunkBytes) noexcept;

  basic_symbol_table(const basic_symbol_table &) = delete;
  basic_symbol_table &operator=(const basic_symbol_table &) = delete;

  basic_symbol_table(basic_symbol_table &&) noexcept = default;
  basic_symbol_table &operator=(basic_symbol_table &&) noexcept = default;

  ~basic_symbol_table() = default;

  bool load(const path_type &path) noexcept;
  bool create(const path_type &path, std::initializer_list<view_type> symbols = {}) noexcept;

  open_code open(const path_type &path, open_mode mode = open_mode::kLoadOrCreate,
                 std::initializer_list<view_type> symbols = {}) noexcept;
  void close();
  void sync() { pool_.sync(); }

  bool is_open() const noexcept { return pool_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  size_type chunk_bytes() const noexcept { return pool_.chunk_bytes(); }
  void set_chunk_bytes(size_type chunk_bytes) noexcept { pool_.set_chunk_bytes(chunk_bytes); }

  size_type size() const noexcept { return symbols_.size(); }
  size_type symbol_count() const noexcept { return size(); }

  bool empty() const noexcept { return size() == 0; }

  iterator begin() noexcept { return iterator(*this); }
  const_iterator begin() const noexcept { return const_iterator(*this); }
  const_iterator cbegin() const noexcept { return const_iterator(*this); }

  iterator end() noexcept { return iterator(*this, static_cast<id_type>(size())); }
  const_iterator end() const noexcept { return const_iterator(*this, static_cast<id_type>(size())); }
  const_iterator cend() const noexcept { return const_iterator(*this, static_cast<id_type>(size())); }

  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }

  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

  view_type get(id_type id) const noexcept { return pool_.get(symbols_[id]); }
  view_type lookup(id_type id) const noexcept { return get(id); }

  iterator find(view_type symbol) const noexcept;
  std::optional<id_type> id(view_type symbol) const noexcept;
  std::optional<id_type> index(view_type symbol) const noexcept { return id(symbol); }

  view_type operator[](id_type id) const noexcept { return get(id); }
  id_type operator[](view_type symbol) const noexcept { return id(symbol).value(); }

  id_type intern(view_type symbol);

private:
  pool_type pool_;
  std::vector<offset_type> symbols_;
  basic_pooled_string_map<id_type, char_type, length_type, traits_type> symbol_to_id_;
};

template <class Id, class Len = std::size_t>
using symbol_table = basic_symbol_table<Id, char, Len>;

template <class Id, class Len = std::size_t>
using wsymbol_table = basic_symbol_table<Id, wchar_t, Len>;

template <class Id, class Len = std::size_t>
using u16symbol_table = basic_symbol_table<Id, uint16_t, Len>;

template <class Id, class Len = std::size_t>
using u32symbol_table = basic_symbol_table<Id, uint32_t, Len>;

#include "details/symbol_table.ipp"
