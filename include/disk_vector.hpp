#pragma once
#include <algorithm>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <mio/mio.hpp>
#include "config.hpp"

template <class T>
class DiskVector {
public:
  using value_type = T;
  using pointer = value_type *;
  using const_pointer = const value_type *;
  using reference = value_type &;
  using const_reference = const value_type &;
  using iterator = pointer;
  using const_iterator = const_pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using path_type = std::filesystem::path;

  DiskVector(size_type chunk_bytes = kDefaultChunkBytes) noexcept : chunk_bytes_{chunk_bytes} {}
  DiskVector(const path_type &path, OpenMode mode = kLoadOrCreate, size_type chunk_bytes = kDefaultChunkBytes) noexcept;
  ~DiskVector() { close(); }

  OpenCode open(const path_type &path, OpenMode mode = kLoadOrCreate) noexcept;
  void close();
  void sync();
  bool is_open() const noexcept { return mmap_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  static size_type element_size() noexcept { return sizeof(T); }
  size_type size() const noexcept { return header().size; }
  size_type length() const noexcept { return size(); }
  size_type capacity() const noexcept { return (mmap_.size() - header_size()) / element_size(); }
  size_type chunk_bytes() const noexcept { return chunk_bytes_; }
  void set_chunk_bytes(size_type chunk_bytes) noexcept { chunk_bytes_ = chunk_bytes; }

  iterator begin() noexcept { return iterator{data()}; }
  const_iterator begin() const noexcept { return const_iterator{data()}; }
  const_iterator cbegin() const noexcept { return const_iterator{data()}; }
  iterator end() noexcept { return iterator{data() + size()}; }
  const_iterator end() const noexcept { return const_iterator{data() + size()}; }
  const_iterator cend() const noexcept { return const_iterator{data() + size()}; }
  reverse_iterator rbegin() noexcept { return reverse_iterator{end()}; }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator{end()}; }
  const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator{end()}; }
  reverse_iterator rend() noexcept { return reverse_iterator{begin()}; }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator{begin()}; }
  const_reverse_iterator crend() const noexcept { return const_reverse_iterator{begin()}; }

  pointer data() noexcept { return reinterpret_cast<pointer>(mmap_.data() + header_size()); }
  const_pointer data() const noexcept { return reinterpret_cast<const_pointer>(mmap_.data() + header_size()); }
  reference operator[](size_type index) noexcept { return data()[index]; }
  const_reference operator[](size_type index) const noexcept { return data()[index]; }
  reference at(size_type index);
  const_reference at(size_type index) const;
  reference front() noexcept { return data()[0]; }
  const_reference front() const noexcept { return data()[0]; }
  reference back() noexcept { return data()[size() - 1]; }
  const_reference back() const noexcept { return data()[size() - 1]; }

  void reserve(size_type capacity);
  void resize(size_type size);
  void clear() { resize(0); }

  reference push_back(const_reference value);
  reference push_back(value_type &&value);
  template <class... Args> reference emplace_back(Args &&... args);
  DiskVector &append(const_reference value);
  DiskVector &append(value_type &&value);
  template <class It> DiskVector &append(It first, It last);

private:
  mio::mmap_sink mmap_;
  path_type path_;
  size_type chunk_bytes_;

  struct Header {
    size_type size;
    size_type element_size;
    size_type magic;
  };

  constexpr static size_type kMagic = 0x544345564b534944; // "DISKVECT"

  static size_type header_size() noexcept { return sizeof(Header); }
  Header &header() noexcept { return *reinterpret_cast<Header *>(mmap_.data()); }
  const Header &header() const noexcept { return *reinterpret_cast<const Header *>(mmap_.data()); }
  bool validate_header() const noexcept;
};

template <class T>
DiskVector<T>::DiskVector(const path_type &path, OpenMode mode, size_type chunk_bytes) noexcept
  : chunk_bytes_{chunk_bytes} { open(path, mode); }

template <class T>
bool DiskVector<T>::validate_header() const noexcept {
  return header().magic == kMagic && header().element_size == element_size();
}

template <class T>
OpenCode DiskVector<T>::open(const path_type &path, OpenMode mode) noexcept {
  namespace fs = std::filesystem;
  std::error_code ec;
  close();
  path_ = path;
  switch (mode) {
  case kLoad: {
    if (!fs::exists(path_) || !fs::is_regular_file(path_)) return kOpenFailed;
    std::size_t fsize = fs::file_size(path_, ec);
    if (ec || fsize < header_size()) return kOpenFailed;
    break;
  }
  case kLoadOrCreate:
    if (fs::exists(path_)) {
      std::size_t fsize = fs::file_size(path_, ec);
      if (!ec && fsize >= header_size()) break;
    }
    [[fallthrough]];
  case kCreate: {
    fs::create_directories(path_.parent_path(), ec);
    if (ec || !std::ofstream{path_, std::ios::app}.good()) return kOpenFailed;
    fs::resize_file(path_, chunk_bytes_, ec);
    if (ec) return kOpenFailed;
    break;
  }
  default:
    return kOpenFailed;
  }

  mmap_.map(path_.string(), ec);
  if (ec) return kOpenFailed;
  switch (mode) {
  case kLoad:
    if (validate_header()) return kLoadSuccess;
    mmap_.unmap();
    return kOpenFailed;
  case kLoadOrCreate:
    if (validate_header()) return kLoadSuccess;
    [[fallthrough]];
  case kCreate:
    header().size = 0;
    header().element_size = element_size();
    header().magic = kMagic;
  }
  return kCreateSuccess;
}

template <class T>
void DiskVector<T>::close() {
  if (!is_open()) return;
  sync();
  mmap_.unmap();
}

template <class T>
void DiskVector<T>::sync() {
  if (!is_open()) return;
  std::error_code error;
  mmap_.sync(error);
  if (error) throw std::system_error{error};
}

template <class T>
auto DiskVector<T>::at(size_type index) -> reference {
  if (index >= size()) throw std::out_of_range{"index out of range"};
  return data()[index];
}

template <class T>
auto DiskVector<T>::at(size_type index) const -> const_reference {
  if (index >= size()) throw std::out_of_range{"index out of range"};
  return data()[index];
}

template <class T>
void DiskVector<T>::reserve(size_type capacity) {
  if (capacity <= this->capacity()) return;
  sync();
  mmap_.unmap();
  std::error_code ec;
  size_type chunk_count = (header_size() + capacity * element_size() + chunk_bytes_ - 1) / chunk_bytes_;
  std::filesystem::resize_file(path_, chunk_count * chunk_bytes_, ec);
  if (ec) throw std::system_error{ec};
  mmap_.map(path_.string(), ec);
  if (ec) throw std::system_error{ec};
}

template <class T>
void DiskVector<T>::resize(size_type size) {
  if (size > this->size()) {
    reserve(size);
    for (iterator it = data() + this->size(); it != data() + size; ++it) std::construct_at(it);
  } else for (iterator it = data() + size; it != data() + this->size(); ++it) std::destroy_at(it);
  header().size = size;
}

template <class T>
auto DiskVector<T>::push_back(const_reference value) -> reference {
  reserve(size() + 1);
  std::construct_at(data() + size(), value);
  ++header().size;
  return back();
}

template <class T>
auto DiskVector<T>::push_back(value_type &&value) -> reference {
  reserve(size() + 1);
  std::construct_at(data() + size(), std::move(value));
  ++header().size;
  return back();
}

template <class T>
template <class... Args>
auto DiskVector<T>::emplace_back(Args &&... args) -> reference {
  reserve(size() + 1);
  std::construct_at(data() + size(), std::forward<Args>(args)...);
  ++header().size;
  return back();
}

template <class T>
auto DiskVector<T>::append(const_reference value) -> DiskVector & {
  reserve(size() + 1);
  std::construct_at(data() + size(), value);
  ++header().size;
  return *this;
}

template <class T>
auto DiskVector<T>::append(value_type &&value) -> DiskVector & {
  reserve(size() + 1);
  std::construct_at(data() + size(), std::move(value));
  ++header().size;
  return *this;
}

template <class T>
template <class It>
auto DiskVector<T>::append(It first, It last) -> DiskVector & {
  auto count = std::distance(first, last);
  reserve(size() + count);
  for (iterator it = data() + size(); first != last; ++it, ++first) std::construct_at(it, *first);
  header().size += count;
  return *this;
}
