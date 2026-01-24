#pragma once

template <class Char, class Traits>
basic_symbol_table_iterator<Char, Traits>::basic_symbol_table_iterator(handle_iterator iterator,
                                                                       const pool_type &symbols) noexcept
  : iterator_(iterator), symbols_(symbols) {}

template <class Char, class Traits>
auto basic_symbol_table_iterator<Char, Traits>::operator++() noexcept -> basic_symbol_table_iterator & {
  ++iterator_;
  return *this;
}

template <class Char, class Traits>
auto basic_symbol_table_iterator<Char, Traits>::operator++(int) noexcept -> basic_symbol_table_iterator {
  auto temp = *this;
  ++iterator_;
  return temp;
}

template <class Char, class Traits>
auto basic_symbol_table_iterator<Char, Traits>::operator--() noexcept -> basic_symbol_table_iterator & {
  --iterator_;
  return *this;
}

template <class Char, class Traits>
auto basic_symbol_table_iterator<Char, Traits>::operator--(int) noexcept -> basic_symbol_table_iterator {
  auto temp = *this;
  --iterator_;
  return temp;
}

template <class Id, class Char, class Traits>
basic_symbol_table<Id, Char, Traits>::basic_symbol_table(size_type chunk_bytes) noexcept
  : symbols_(chunk_bytes), symbol_to_id_(0, symbols_, symbols_) {}

template <class Id, class Char, class Traits>
basic_symbol_table<Id, Char, Traits>::basic_symbol_table(
  const path_type &path, open_mode mode, std::initializer_list<view_type> symbols, size_type chunk_bytes) noexcept
  : basic_symbol_table(chunk_bytes) {
  open(path, mode, symbols, chunk_bytes);
}

template <class Id, class Char, class Traits>
open_code basic_symbol_table<Id, Char, Traits>::open(const path_type &path, open_mode mode,
                                                     std::initializer_list<view_type> symbols) noexcept {
  close();
  auto code = symbols_.open(path, mode);
  if (code == open_code::kCreateSuccess) for (auto symbol : symbols) add(symbol);
  if (code == open_code::kLoadSuccess)
    for (auto it = symbols_.begin(); it != symbols_.end(); ++it) {
      auto handle = it.handle();
      id_type id = id_to_symbol_.size();
      id_to_symbol_.emplace_back(handle);
      symbol_to_id_.emplace(handle, id);
    }
  return code;
}

template <class Id, class Char, class Traits>
void basic_symbol_table<Id, Char, Traits>::close() {
  symbols_.close();
  id_to_symbol_.clear();
  symbol_to_id_.clear();
}

template <class Id, class Char, class Traits>
auto basic_symbol_table<Id, Char, Traits>::id(view_type symbol) const noexcept -> std::optional<Id> {
  auto it = symbol_to_id_.find(symbol);
  if (it != symbol_to_id_.end()) return it->second;
  return std::nullopt;
}

template <class Id, class Char, class Traits>
auto basic_symbol_table<Id, Char, Traits>::add(view_type symbol) -> id_type {
  auto it = symbol_to_id_.find(symbol);
  if (it != symbol_to_id_.end()) return it->second;
  auto handle = symbols_.add(symbol);
  id_type id = id_to_symbol_.size();
  id_to_symbol_.emplace_back(handle);
  symbol_to_id_.emplace(handle, id);
  return id;
}
