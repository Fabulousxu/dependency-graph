#pragma once
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <mio/mio.hpp>
#include "config.hpp"
#include "types.hpp"
#include "utils.hpp"

namespace xpg {

template <class T>
class mmap_vector {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type *;
  using const_pointer = const value_type *;
  using reference = value_type &;
  using const_reference = const value_type &;
  using iterator = pointer;
  using const_iterator = const_pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  mmap_vector(size_type growth_bytes = kGrowthBytes) noexcept : growth_bytes_(growth_bytes) {}
  mmap_vector(const std::filesystem::path &path, open_mode mode = open_mode::kLoadOrCreate,
              size_type growth_bytes = kGrowthBytes) : growth_bytes_(growth_bytes) { open(path, mode); }
  mmap_vector(const mmap_vector &) = delete;
  mmap_vector &operator=(const mmap_vector &) = delete;
  mmap_vector(mmap_vector &&) noexcept = default;
  mmap_vector &operator=(mmap_vector &&) noexcept = default;
  ~mmap_vector() { close(); }

  void load(const std::filesystem::path &path) {
    close();
    mmap_ = path.string();
    if (std::filesystem::file_size(path) < sizeof(header_t) || header().magic != kMagic
      || header().element_size != sizeof(value_type))
      throw std::runtime_error(std::format("File '{}' is not a valid mmap_vector.", path.string()));
    path_ = path;
  }

  void create(const std::filesystem::path &path) {
    close();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc);
    std::filesystem::resize_file(path, sizeof(header_t) + growth_bytes_);
    mmap_ = path.string();
    header() = {kMagic, sizeof(value_type), 0};
    path_ = path;
  }

  void open(const std::filesystem::path &path, open_mode mode = open_mode::kLoadOrCreate) {
    if (mode == open_mode::kLoad) load(path);
    else if (mode == open_mode::kCreate) create(path);
    else if (mode == open_mode::kLoadOrCreate) std::filesystem::exists(path) ? load(path) : create(path);
    else throw std::invalid_argument("Invalid open_mode.");
  }

  void close() {
    if (!is_open()) return;
    sync();
    mmap_.unmap();
    path_.clear();
  }

  void sync() {
    std::error_code ec;
    mmap_.sync(ec);
    if (ec) throw std::system_error(ec);
  }

  bool is_open() const noexcept { return mmap_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  size_type growth_bytes() const noexcept { return growth_bytes_; }
  void set_growth_bytes(size_type growth_bytes) noexcept { growth_bytes_ = growth_bytes; }
  size_type size() const noexcept { return header().size; }
  size_type capacity() const noexcept { return (mmap_.size() - sizeof(header_t)) / sizeof(value_type); }
  bool empty() const noexcept { return size() == 0; }

  iterator begin() noexcept { return data(); }
  iterator end() noexcept { return data() + size(); }
  const_iterator begin() const noexcept { return data(); }
  const_iterator end() const noexcept { return data() + size(); }
  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator cend() const noexcept { return end(); }
  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
  const_reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  const_reverse_iterator crend() const noexcept { return rend(); }

  pointer data() noexcept { return reinterpret_cast<pointer>(mmap_.data() + sizeof(header_t)); }
  const_pointer data() const noexcept { return reinterpret_cast<const_pointer>(mmap_.data() + sizeof(header_t)); }
  reference operator[](size_type pos) noexcept { return data()[pos]; }
  const_reference operator[](size_type pos) const noexcept { return data()[pos]; }
  reference at(size_type pos) noexcept { return (*this)[pos]; }
  const_reference at(size_type pos) const noexcept { return (*this)[pos]; }
  reference front() noexcept { return (*this)[0]; }
  const_reference front() const noexcept { return (*this)[0]; }
  reference back() noexcept { return (*this)[size() - 1]; }
  const_reference back() const noexcept { return (*this)[size() - 1]; }

  void reserve(size_type capacity) {
    if (capacity <= this->capacity()) return;
    sync();
    mmap_.unmap();
    std::filesystem::resize_file(path_, alignup(sizeof(header_t) + capacity * sizeof(value_type), growth_bytes_));
    mmap_ = path_.string();
  }

  void resize(size_type size) {
    if (size > this->size()) {
      reserve(size);
      for (auto ptr = data() + this->size(); ptr != data() + size; ++ptr) std::construct_at(ptr);
    } else for (auto ptr = data() + size; ptr != data() + this->size(); ++ptr) std::destroy_at(ptr);
    header().size = size;
  }

  void clear() { resize(0); }

  reference push_back(const_reference value) {
    reserve(size() + 1);
    std::construct_at(data() + size(), value);
    ++header().size;
    return back();
  }

  reference push_back(value_type &&value) {
    reserve(size() + 1);
    std::construct_at(data() + size(), std::move(value));
    ++header().size;
    return back();
  }

  template <class... Args> requires std::constructible_from<value_type, Args &&...>
  reference emplace_back(Args &&... args) {
    reserve(size() + 1);
    std::construct_at(data() + size(), std::forward<Args>(args)...);
    ++header().size;
    return back();
  }

  mmap_vector &append(const_reference value) {
    reserve(size() + 1);
    std::construct_at(data() + size(), value);
    ++header().size;
    return *this;
  }

  mmap_vector &append(value_type &&value) {
    reserve(size() + 1);
    std::construct_at(data() + size(), std::move(value));
    ++header().size;
    return *this;
  }

  template <std::forward_iterator It, std::sentinel_for<It> S>
    requires std::constructible_from<value_type, std::iter_reference_t<It>>
  mmap_vector &append(It first, S last) {
    auto count = std::ranges::distance(first, last);
    reserve(size() + count);
    for (auto ptr = data() + size(); first != last; ++ptr, ++first) std::construct_at(ptr, *first);
    header().size += count;
    return *this;
  }

  template <std::ranges::forward_range R>
    requires std::constructible_from<value_type, std::ranges::range_reference_t<R>>
  mmap_vector &append(R &&r) {
    auto count = std::ranges::distance(r);
    reserve(size() + count);
    auto out = data() + size();
    for (auto &&value : r) std::construct_at(out++, value);
    header().size += count;
    return *this;
  }

private:
  struct header_t {
    size_type magic;
    size_type element_size;
    size_type size;
  };

  mio::mmap_sink mmap_;
  std::filesystem::path path_;
  size_type growth_bytes_;
  static constexpr size_type kMagic = 0x5443455650414d4d; // "MMAPVECT"

  header_t &header() noexcept { return *reinterpret_cast<header_t *>(mmap_.data()); }
  const header_t &header() const noexcept { return *reinterpret_cast<const header_t *>(mmap_.data()); }
};

} // namespace xpg
