#pragma once
#include <concepts>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include "config.hpp"
#include "string_map.hpp"
#include "string_pool.hpp"

template <class Id, class Char, class Traits = std::char_traits<Char>>
class BasicSymbolTable {
public:
  static_assert(std::integral<Id> || std::is_enum_v<Id>);
  using id_type = Id;
  using char_type = Char;
  using traits_type = Traits;
  using view_type = std::basic_string_view<char_type, traits_type>;
  using size_type = std::size_t;
  using path_type = BasicStringPool<char_type, true, traits_type>::path_type;

  BasicSymbolTable(size_type chunk_bytes = kSmallChunkBytes) noexcept;
  BasicSymbolTable(const path_type &path, OpenMode mode = kLoadOrCreate, std::initializer_list<view_type> symbols = {},
    size_type chunk_bytes = kSmallChunkBytes) noexcept;

  OpenCode open(const path_type &path, OpenMode mode = kLoadOrCreate,
    std::initializer_list<view_type> symbols = {}) noexcept;
  void close();
  void sync() noexcept { symbols_.sync(); }
  bool is_open() const noexcept { return symbols_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  size_type size() const noexcept { return id_to_symbols_.size(); }
  size_type symbol_count() const noexcept { return size(); }
  size_type chunk_bytes() const noexcept { return symbols_.chunk_bytes(); }
  void set_chunk_bytes(size_type chunk_bytes) noexcept { symbols_.set_chunk_bytes(chunk_bytes); }

  view_type get(id_type id) const noexcept;
  std::optional<id_type> id(view_type symbol) const noexcept;
  view_type operator[](id_type id) const noexcept { return get(id); }
  std::optional<id_type> operator[](view_type symbol) const noexcept { return id(symbol); }
  id_type add(view_type symbol) noexcept;

private:
  BasicStringPool<Char, true, Traits> symbols_;
  std::vector<StringHandle> id_to_symbols_;
  BasicStringHandleMap<char_type, id_type, true, traits_type> symbol_to_id_;
};

template <class Id>
using SymbolTable = BasicSymbolTable<Id, char>;


template <class Id, class Char, class Traits>
BasicSymbolTable<Id, Char, Traits>::BasicSymbolTable(size_type chunk_bytes) noexcept
  : symbols_{chunk_bytes}, symbol_to_id_{
      0, BasicStringHandleHash<char_type, true, traits_type>{symbols_},
      BasicStringHandleEqual<char_type, true, traits_type>{symbols_}
    } {}

template <class Id, class Char, class Traits>
BasicSymbolTable<Id, Char, Traits>::BasicSymbolTable(const path_type &path, OpenMode mode,
  std::initializer_list<view_type> symbols, size_type chunk_bytes) noexcept
  : symbols_{chunk_bytes}, symbol_to_id_{0, {symbols_}, {symbols_}} {
  open(path, mode, symbols, chunk_bytes);
}

template <class Id, class Char, class Traits>
OpenCode BasicSymbolTable<Id, Char, Traits>::open(const path_type &path, OpenMode mode,
  std::initializer_list<view_type> symbols) noexcept {
  if (is_open()) close();
  auto oc = symbols_.open(path, mode);
  if (oc == kCreateSuccess) for (auto symbol : symbols) add(symbol);
  if (oc == kLoadSuccess)
    for (auto it = symbols_.begin(); it != symbols_.end(); ++it) {
      auto handle = it.handle();
      id_type id = id_to_symbols_.size();
      id_to_symbols_.emplace_back(handle);
      symbol_to_id_.emplace(handle, id);
    }
  return oc;
}

template <class Id, class Char, class Traits>
void BasicSymbolTable<Id, Char, Traits>::close() {
  symbols_.close();
  id_to_symbols_.clear();
  symbol_to_id_.clear();
}

template <class Id, class Char, class Traits>
auto BasicSymbolTable<Id, Char, Traits>::get(id_type id) const noexcept -> view_type {
  auto handle = id_to_symbols_[static_cast<std::size_t>(id)];
  return symbols_.get(handle);
}

template <class Id, class Char, class Traits>
auto BasicSymbolTable<Id, Char, Traits>::id(view_type symbol) const noexcept -> std::optional<Id> {
  auto it = symbol_to_id_.find(symbol);
  if (it != symbol_to_id_.end()) return it->second;
  return std::nullopt;
}

template <class Id, class Char, class Traits>
auto BasicSymbolTable<Id, Char, Traits>::add(view_type symbol) noexcept -> id_type {
  auto it = symbol_to_id_.find(symbol);
  if (it != symbol_to_id_.end()) return it->second;
  id_type id = id_to_symbols_.size();
  auto handle = symbols_.add(symbol);
  id_to_symbols_.emplace_back(handle);
  symbol_to_id_.emplace(handle, id);
  return id;
}
