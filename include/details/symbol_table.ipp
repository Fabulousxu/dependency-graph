#pragma once

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table_iterator<Id, Char, Len, Traits>::operator++() noexcept -> basic_symbol_table_iterator & {
  ++id_;
  return *this;
}

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table_iterator<Id, Char, Len, Traits>::operator++(int) noexcept -> basic_symbol_table_iterator {
  basic_symbol_table_iterator temp = *this;
  ++*this;
  return temp;
}

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table_iterator<Id, Char, Len, Traits>::operator--() noexcept -> basic_symbol_table_iterator & {
  --id_;
  return *this;
}

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table_iterator<Id, Char, Len, Traits>::operator--(int) noexcept -> basic_symbol_table_iterator {
  basic_symbol_table_iterator temp = *this;
  --*this;
  return temp;
}

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table_iterator<Id, Char, Len, Traits>::operator+=(difference_type n) noexcept
  -> basic_symbol_table_iterator & {
  id_ += static_cast<id_type>(n);
  return *this;
}

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table_iterator<Id, Char, Len, Traits>::operator-=(difference_type n) noexcept
  -> basic_symbol_table_iterator & {
  id_ -= static_cast<id_type>(n);
  return *this;
}

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table_iterator<Id, Char, Len, Traits>::operator+(difference_type n) const noexcept
  -> basic_symbol_table_iterator {
  basic_symbol_table_iterator temp = *this;
  temp += n;
  return temp;
}

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table_iterator<Id, Char, Len, Traits>::operator-(difference_type n) const noexcept
  -> basic_symbol_table_iterator {
  basic_symbol_table_iterator temp = *this;
  temp -= n;
  return temp;
}

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table_iterator<Id, Char, Len, Traits>::operator-(const basic_symbol_table_iterator &other) const
  noexcept -> difference_type {
  return static_cast<difference_type>(id_) - static_cast<difference_type>(other.id_);
}


template <class Id, class Char, class Len, class Traits>
basic_symbol_table<Id, Char, Len, Traits>::basic_symbol_table(size_type chunk_bytes) noexcept
  : pool_(chunk_bytes), symbol_to_id_(0, pool_, pool_) {}

template <class Id, class Char, class Len, class Traits>
basic_symbol_table<Id, Char, Len, Traits>::basic_symbol_table(
  const path_type &path, open_mode mode, std::initializer_list<view_type> symbols, size_type chunk_bytes) noexcept
  : basic_symbol_table(chunk_bytes) {
  open(path, mode, symbols);
}

template <class Id, class Char, class Len, class Traits>
bool basic_symbol_table<Id, Char, Len, Traits>::load(const path_type &path) noexcept {
  close();
  if (!pool_.load(path)) return false;
  for (auto it = pool_.begin(); it != pool_.end(); ++it) {
    auto id = static_cast<id_type>(symbols_.size());
    symbols_.emplace_back(it.offset());
    symbol_to_id_.emplace(it.offset(), id);
  }
  return true;
}

template <class Id, class Char, class Len, class Traits>
bool basic_symbol_table<Id, Char, Len, Traits>::create(const path_type &path,
                                                       std::initializer_list<view_type> symbols) noexcept {
  close();
  if (!pool_.create(path)) return false;
  for (auto symbol : symbols) intern(symbol);
  return true;
}

template <class Id, class Char, class Len, class Traits>
open_code basic_symbol_table<Id, Char, Len, Traits>::open(const path_type &path, open_mode mode,
                                                          std::initializer_list<view_type> symbols) noexcept {
  switch (mode) {
  case open_mode::kLoad:
    if (load(path)) return open_code::kLoadSuccess;
    return open_code::kOpenFailed;
  case open_mode::kCreate:
    if (create(path, symbols)) return open_code::kCreateSuccess;
    return open_code::kOpenFailed;
  case open_mode::kLoadOrCreate:
    if (load(path)) return open_code::kLoadSuccess;
    if (create(path, symbols)) return open_code::kCreateSuccess;
    return open_code::kOpenFailed;
  default:
    return open_code::kOpenFailed;
  }
}

template <class Id, class Char, class Len, class Traits>
void basic_symbol_table<Id, Char, Len, Traits>::close() {
  pool_.close();
  symbols_.clear();
  symbol_to_id_.clear();
}

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table<Id, Char, Len, Traits>::find(view_type symbol) const noexcept -> iterator {
  auto it = symbol_to_id_.find(symbol);
  if (it != symbol_to_id_.end()) return iterator(*this, it->second);
  return end();
}

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table<Id, Char, Len, Traits>::id(view_type symbol) const noexcept -> std::optional<id_type> {
  auto it = symbol_to_id_.find(symbol);
  if (it != symbol_to_id_.end()) return it->second;
  return std::nullopt;
}

template <class Id, class Char, class Len, class Traits>
auto basic_symbol_table<Id, Char, Len, Traits>::intern(view_type symbol) -> id_type {
  auto it = symbol_to_id_.find(symbol);
  if (it != symbol_to_id_.end()) return it->second;
  auto id = static_cast<id_type>(symbols_.size());
  auto offset = pool_.add(symbol);
  symbols_.emplace_back(offset);
  symbol_to_id_.emplace(offset, id);
  return id;
}
