#pragma once
#include <algorithm>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <mio/mio.hpp>

constexpr std::size_t kDefaultChunkBytes = 1 * 1024 * 1024;

template <class T>
class DiskVector {
public:
  using value_type = T;
  using pointer = T *;
  using const_pointer = const T *;
  using reference = T &;
  using const_reference = const T &;
  using iterator = T *;
  using const_iterator = const T *;
  using reverse_iterator = std::reverse_iterator<T *>;
  using const_reverse_iterator = std::reverse_iterator<const T *>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  DiskVector(std::string_view path, std::size_t chunk_bytes = kDefaultChunkBytes)
    : path_{path}, chunk_bytes_{chunk_bytes}, size_{0} {
    if (std::filesystem::exists(path_)) {
      if (!std::filesystem::is_regular_file(path_)) throw std::ios_base::failure{"Not a regular file: " + path_};
    } else if (!std::ofstream{path_}) throw std::ios_base::failure{"Failed to create file: " + path_};
    auto file_size = std::filesystem::file_size(path_);
    auto chunk_count = file_size != 0 ? (file_size + chunk_bytes_ - 1) / chunk_bytes_ : 1;
    std::filesystem::resize_file(path_, chunk_count * chunk_bytes_);
    std::error_code error;
    mmap_.map(path_, error);
    if (error) throw std::ios_base::failure{"Failed to map file: " + path_};
    data_ = reinterpret_cast<T *>(mmap_.data());
  }

  ~DiskVector() { sync(); }

  std::size_t size() const noexcept { return size_; }
  std::size_t length() const noexcept { return size_; }
  std::size_t capacity() const noexcept { return mmap_.size() / sizeof(T); }

  iterator begin() noexcept { return data_; }
  const_iterator begin() const noexcept { return data_; }
  const_iterator cbegin() const noexcept { return data_; }
  reverse_iterator rbegin() noexcept { return reverse_iterator{data_ + size_}; }
  const_reverse_iterator rbegin() const noexcept { return reverse_iterator{data_ + size_}; }
  const_reverse_iterator crbegin() const noexcept { return reverse_iterator{data_ + size_}; }
  iterator end() noexcept { return data_ + size_; }
  const_iterator end() const noexcept { return data_ + size_; }
  const_iterator cend() const noexcept { return data_ + size_; }
  reverse_iterator rend() noexcept { return reverse_iterator{data_}; }
  const_reverse_iterator rend() const noexcept { return reverse_iterator{data_}; }
  const_reverse_iterator crend() const noexcept { return reverse_iterator{data_}; }

  pointer data() noexcept { return data_; }
  const_pointer data() const noexcept { return data_; }
  reference operator[](std::size_t index) noexcept { return data_[index]; }
  const_reference operator[](std::size_t index) const noexcept { return data_[index]; }
  reference at(std::size_t index) noexcept { return data_[index]; }
  const_reference at(std::size_t index) const noexcept { return data_[index]; }
  reference front() noexcept { return data_[0]; }
  const_reference front() const noexcept { return data_[0]; }
  reference back() noexcept { return data_[size_ - 1]; }
  const_reference back() const noexcept { return data_[size_ - 1]; }

  void clear() noexcept { size_ = 0; }

  void resize(std::size_t new_size) {
    reserve(new_size);
    size_ = new_size;
  }

  void reserve(std::size_t new_size) {
    if (new_size > capacity()) reserve_chunks(((new_size - capacity()) * sizeof(T) + chunk_bytes_ - 1) / chunk_bytes_);
  }

  void reserve_chunks(std::size_t chunk_count = 1) {
    if (chunk_count == 0) return;
    mmap_.unmap();
    std::filesystem::resize_file(path_, std::filesystem::file_size(path_) + chunk_count * chunk_bytes_);
    std::error_code error;
    mmap_.map(path_, error);
    if (error) throw std::ios_base::failure{"Failed to map file: " + path_};
    data_ = reinterpret_cast<T *>(mmap_.data());
  }

  reference push_back(const T &value) {
    resize(size_ + 1);
    return data_[size_ - 1] = value;
  }

  reference push_back(T &&value) {
    resize(size_ + 1);
    return data_[size_ - 1] = std::move(value);
  }

  reference append(const T &value) { return push_back(value); }
  reference append(T &&value) { return push_back(std::move(value)); }

  template <class It>
  void append(It first, It last) {
    auto count = std::distance(first, last);
    resize(size_ + count);
    std::copy_n(first, count, data_ + size_ - count);
  }

  template <class... Args> requires std::constructible_from<T, Args...>
  reference emplace_back(Args &&... args) {
    resize(size_ + 1);
    new(&data_[size_ - 1]) T{std::forward<Args>(args)...};
    return data_[size_ - 1];
  }

  void sync() {
    std::error_code error;
    mmap_.sync(error);
    if (error) throw std::ios_base::failure{"Failed to sync file: " + path_};
  }

private:
  std::string path_;
  mio::mmap_sink mmap_;
  T *data_;
  std::size_t size_;
  std::size_t chunk_bytes_;
};
