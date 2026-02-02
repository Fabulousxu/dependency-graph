#pragma once
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>
#include <type_traits>
#include <utility>
#include "util.hpp"

template <class T>
mmap_vector<T>::mmap_vector(const path_type &path, open_mode mode, size_type chunk_bytes) noexcept
  : mmap_vector(chunk_bytes) { open(path, mode); }

template <class T>
bool mmap_vector<T>::validate_header() const noexcept {
  return header().magic == header_magic && header().element_size == element_size();
}

template <class T>
bool mmap_vector<T>::load(const path_type &path) noexcept {
  close();
  path_ = path;
  std::error_code error;
  auto exists = std::filesystem::exists(path_, error);
  if (error || !exists) return false;
  auto is_regular_file = std::filesystem::is_regular_file(path_, error);
  if (error || !is_regular_file) return false;
  auto file_size = std::filesystem::file_size(path_, error);
  if (error || file_size < header_size()) return false;
  mmap_.map(path_.string(), error);
  if (error) return false;
  if (validate_header()) return true;
  mmap_.unmap();
  return false;
}

template <class T>
bool mmap_vector<T>::create(const path_type &path) noexcept {
  close();
  path_ = path;
  std::error_code error;
  std::filesystem::create_directories(path_.parent_path(), error);
  if (error || !std::ofstream(path_, std::ios::binary | std::ios::trunc).good()) return false;
  std::filesystem::resize_file(path_, chunk_bytes_, error);
  if (error) return false;
  mmap_.map(path_.string(), error);
  if (error) return false;
  header() = {
    .magic = header_magic,
    .element_size = element_size(),
    .size = 0
  };
  return true;
}

template <class T>
open_code mmap_vector<T>::open(const path_type &path, open_mode mode) noexcept {
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

template <class T>
void mmap_vector<T>::close() {
  if (!is_open()) return;
  sync();
  mmap_.unmap();
  path_.clear();
}

template <class T>
void mmap_vector<T>::sync() {
  std::error_code error;
  mmap_.sync(error);
  if (error) throw std::system_error(error);
}

template <class T>
void mmap_vector<T>::reserve(size_type new_capacity) {
  if (new_capacity <= capacity()) return;
  sync();
  mmap_.unmap();
  std::error_code error;
  std::filesystem::resize_file(path_, alignup(header_size() + new_capacity * element_size(), chunk_bytes_), error);
  if (error) throw std::system_error(error);
  mmap_.map(path_.string(), error);
  if (error) throw std::system_error(error);
}

template <class T>
void mmap_vector<T>::resize(size_type new_size) {
  if (new_size > size()) {
    reserve(new_size);
    for (auto it = data() + size(); it != data() + new_size; ++it) std::construct_at(it);
  } else for (auto it = data() + new_size; it != data() + size(); ++it) std::destroy_at(it);
  header().size = new_size;
}

template <class T>
auto mmap_vector<T>::push_back(const_reference value) -> reference {
  reserve(size() + 1);
  std::construct_at(data() + size(), value);
  ++header().size;
  return back();
}

template <class T>
auto mmap_vector<T>::push_back(value_type &&value) -> reference {
  reserve(size() + 1);
  std::construct_at(data() + size(), std::move(value));
  ++header().size;
  return back();
}

template <class T>
template <class... Args>
auto mmap_vector<T>::emplace_back(Args &&... args) -> reference {
  reserve(size() + 1);
  std::construct_at(data() + size(), std::forward<Args>(args)...);
  ++header().size;
  return back();
}

template <class T>
auto mmap_vector<T>::append(const_reference value) -> mmap_vector & {
  reserve(size() + 1);
  std::construct_at(data() + size(), value);
  ++header().size;
  return *this;
}

template <class T>
auto mmap_vector<T>::append(value_type &&value) -> mmap_vector & {
  reserve(size() + 1);
  std::construct_at(data() + size(), std::move(value));
  ++header().size;
  return *this;
}

template <class T>
template <class It>
auto mmap_vector<T>::append(It first, It last) -> mmap_vector & {
  auto count = std::distance(first, last);
  reserve(size() + count);
  if constexpr (std::is_trivially_copyable_v<T> && std::contiguous_iterator<It>)
    std::memcpy(data() + size(), std::to_address(first), count * element_size());
  else for (auto it = data() + size(); first != last; ++it, ++first) std::construct_at(it, *first);
  header().size += count;
  return *this;
}
