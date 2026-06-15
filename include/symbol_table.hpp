#pragma once
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include "string_pool.hpp"

namespace xpg {

template <class IdT, class CharT, class LenT = std::size_t, class Traits = std::char_traits<CharT>>
class basic_symbol_table {
  static_assert(std::integral<IdT> || std::is_enum_v<IdT>);
  static_assert(!std::is_void_v<LenT>);

public:
  using id_type = IdT;
  using char_type = CharT;
  using traits_type = Traits;
  using view_type = std::basic_string_view<char_type, traits_type>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  class iterator;
  using const_iterator = iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  class iterator {
  public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = view_type;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = value_type;

    iterator(const basic_symbol_table &table, id_type id = 0) noexcept : table_(table), id_(id) {}

    iterator &operator++() noexcept {
      ++id_;
      return *this;
    }

    iterator operator++(int) noexcept {
      iterator temp = *this;
      ++*this;
      return temp;
    }

    iterator &operator--() noexcept {
      --id_;
      return *this;
    }

    iterator operator--(int) noexcept {
      iterator temp = *this;
      --*this;
      return temp;
    }

    iterator &operator+=(difference_type n) noexcept {
      id_ += static_cast<id_type>(n);
      return *this;
    }

    iterator &operator-=(difference_type n) noexcept {
      id_ -= static_cast<id_type>(n);
      return *this;
    }

    iterator operator+(difference_type n) const noexcept {
      iterator temp = *this;
      temp += n;
      return temp;
    }

    iterator operator-(difference_type n) const noexcept {
      iterator temp = *this;
      temp -= n;
      return temp;
    }

    friend difference_type operator-(const iterator &l, const iterator &r)
      noexcept { return static_cast<difference_type>(l.id_) - static_cast<difference_type>(r.id_); }
    friend bool operator==(const iterator &l, const iterator &r) noexcept { return l.id_ == r.id_; }
    friend bool operator!=(const iterator &l, const iterator &r) noexcept { return !(l == r); }
    friend auto operator<=>(const iterator &l, const iterator &r) noexcept { return l.id_ <=> r.id_; }

    view_type view() const noexcept { return table_.get(id_); }
    reference operator*() const noexcept { return view(); }
    id_type id() const noexcept { return id_; }
    id_type index() const noexcept { return id(); }

  private:
    const basic_symbol_table &table_;
    id_type id_;
  };

  basic_symbol_table(size_type growth_bytes = 1024) noexcept : pool_(growth_bytes), symbol_to_id_(0, pool_, pool_) {}
  template <std::ranges::input_range R> requires std::convertible_to<std::ranges::range_reference_t<R>, view_type>
  basic_symbol_table(const std::filesystem::path &path, open_mode mode = open_mode::kLoadOrCreate, R &&symbols = {},
                     size_type growth_bytes = 1024) : basic_symbol_table(growth_bytes) { open(path, mode, symbols); }
  basic_symbol_table(const std::filesystem::path &path, open_mode mode = open_mode::kLoadOrCreate,
                     std::initializer_list<view_type> symbols = {}, size_type growth_bytes = 1024)
    : basic_symbol_table(path, mode, std::views::all(symbols), growth_bytes) {}
  basic_symbol_table(const basic_symbol_table &) = delete;
  basic_symbol_table &operator=(const basic_symbol_table &) = delete;
  basic_symbol_table(basic_symbol_table &&) noexcept = default;
  basic_symbol_table &operator=(basic_symbol_table &&) noexcept = default;
  ~basic_symbol_table() = default;

  void load(const std::filesystem::path &path) {
    close();
    pool_.load(path);
    for (auto it = pool_.begin(); it != pool_.end(); ++it) {
      auto id = static_cast<id_type>(symbols_.size());
      symbols_.emplace_back(it.offset());
      symbol_to_id_.emplace(it.offset(), id);
    }
  }

  template <std::ranges::input_range R> requires std::convertible_to<std::ranges::range_reference_t<R>, view_type>
  void create(const std::filesystem::path &path, R &&symbols = {}) {
    close();
    pool_.create(path);
    for (auto symbol : symbols) intern(symbol);
  }

  void create(const std::filesystem::path &path,
              std::initializer_list<view_type> symbols = {}) { create(path, std::views::all(symbols)); }

  template <std::ranges::input_range R> requires std::convertible_to<std::ranges::range_reference_t<R>, view_type>
  void open(const std::filesystem::path &path, open_mode mode = open_mode::kLoadOrCreate, R &&symbols = {}) {
    if (mode == open_mode::kLoad) load(path);
    else if (mode == open_mode::kCreate) create(path, symbols);
    else if (mode == open_mode::kLoadOrCreate) std::filesystem::exists(path) ? load(path) : create(path, symbols);
    else throw std::invalid_argument("Invalid open_mode.");
  }

  void open(const std::filesystem::path &path, open_mode mode = open_mode::kLoadOrCreate,
            std::initializer_list<view_type> symbols = {}) { open(path, mode, std::views::all(symbols)); }

  void close() {
    pool_.close();
    symbols_.clear();
    symbol_to_id_.clear();
  }

  void sync() { pool_.sync(); }
  bool is_open() const noexcept { return pool_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  size_type growth_bytes() const noexcept { return pool_.growth_bytes(); }
  void set_growth_bytes(size_type growth_bytes) noexcept { pool_.set_growth_bytes(growth_bytes); }
  size_type size() const noexcept { return symbols_.size(); }
  bool empty() const noexcept { return size() == 0; }

  const_iterator begin() const noexcept { return const_iterator(*this); }
  const_iterator end() const noexcept { return const_iterator(*this, static_cast<id_type>(size())); }
  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator cend() const noexcept { return cend(); }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(cend()); }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(cbegin()); }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  const_reverse_iterator crend() const noexcept { return rend(); }

  view_type get(id_type id) const noexcept { return pool_.get(symbols_[id]); }
  view_type lookup(id_type id) const noexcept { return get(id); }
  view_type operator[](id_type id) const noexcept { return get(id); }

  iterator find(view_type symbol) const noexcept {
    auto it = symbol_to_id_.find(symbol);
    return it != symbol_to_id_.end() ? iterator(*this, it->second) : end();
  }

  std::optional<id_type> id(view_type symbol) const noexcept {
    auto it = symbol_to_id_.find(symbol);
    if (it != symbol_to_id_.end()) return it->second;
    return std::nullopt;
  }

  std::optional<id_type> index(view_type symbol) const noexcept { return id(symbol); }
  id_type operator[](view_type symbol) const noexcept { return id(symbol).value(); }

  id_type intern(view_type symbol) {
    auto it = symbol_to_id_.find(symbol);
    if (it != symbol_to_id_.end()) return it->second;
    auto id = static_cast<id_type>(symbols_.size());
    auto str = pool_.append(symbol);
    symbols_.emplace_back(str);
    symbol_to_id_.emplace(str, id);
    return id;
  }

private:
  basic_string_pool<char_type, LenT, traits_type> pool_;
  std::vector<std::size_t> symbols_;
  basic_pooled_string_map<id_type, char_type, LenT, traits_type> symbol_to_id_;
};

template <class IdT = std::size_t, class LenT = std::size_t>
using symbol_table = basic_symbol_table<IdT, char, LenT>;
template <class IdT = std::size_t, class LenT = std::size_t>
using wsymbol_table = basic_symbol_table<IdT, wchar_t, LenT>;
template <class IdT = std::size_t, class LenT = std::size_t>
using u16symbol_table = basic_symbol_table<IdT, char16_t, LenT>;
template <class IdT = std::size_t, class LenT = std::size_t>
using u32symbol_table = basic_symbol_table<IdT, char32_t, LenT>;

} // namespace xpg
