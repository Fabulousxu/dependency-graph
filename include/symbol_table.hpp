#pragma once
#include <concepts>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include "string_map.hpp"

template <class Id> requires (std::integral<Id> || std::is_enum_v<Id>)
class SymbolTable {
public:
  SymbolTable() = default;
  SymbolTable(std::initializer_list<std::string_view> symbols) noexcept { for (auto symbol : symbols) add(symbol); }
  ~SymbolTable() = default;

  std::size_t size() const noexcept { return symbols_.size(); }
  std::size_t symbol_count() const noexcept { return symbols_.size(); }
  const std::vector<std::string> &symbols() const noexcept { return symbols_; }
  std::string_view get(Id id) const noexcept { return symbols_[static_cast<std::size_t>(id)]; }

  std::optional<Id> id(std::string_view symbol) const noexcept {
    auto it = id_to_symbol_.find(symbol);
    return it != id_to_symbol_.end() ? std::optional{it->second} : std::nullopt;
  }

  Id add(std::string_view symbol) noexcept {
    auto [it, succ] = id_to_symbol_.emplace(symbol, static_cast<Id>(symbols_.size()));
    if (!succ) return it->second;
    symbols_.emplace_back(symbol);
    return static_cast<Id>(symbols_.size() - 1);
  }

private:
  std::vector<std::string> symbols_;
  StringMap<Id> id_to_symbol_;
};
