#pragma once
#include <filesystem>
#include <iterator>
#include <mio/mio.hpp>
#include "config.hpp"

template <class T>
class mmap_vector {
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

  mmap_vector(size_type chunk_bytes = kDefaultChunkBytes) noexcept : chunk_bytes_(chunk_bytes) {}
  mmap_vector(const path_type &path, open_mode mode = open_mode::kLoadOrCreate,
              size_type chunk_bytes = kDefaultChunkBytes) noexcept;

  mmap_vector(const mmap_vector &) = delete;
  mmap_vector &operator=(const mmap_vector &) = delete;

  mmap_vector(mmap_vector &&) noexcept = default;
  mmap_vector &operator=(mmap_vector &&) noexcept = default;

  ~mmap_vector() { close(); }

  bool load(const path_type &path) noexcept;
  bool create(const path_type &path) noexcept;

  open_code open(const path_type &path, open_mode mode = open_mode::kLoadOrCreate) noexcept;
  void close();
  void sync();

  bool is_open() const noexcept { return mmap_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  size_type chunk_bytes() const noexcept { return chunk_bytes_; }
  void set_chunk_bytes(size_type chunk_bytes) noexcept { chunk_bytes_ = chunk_bytes; }

  static constexpr size_type element_size() noexcept { return sizeof(T); }

  size_type size() const noexcept { return header().size; }
  size_type length() const noexcept { return size(); }
  size_type capacity() const noexcept { return (mmap_.size() - header_size()) / element_size(); }

  bool empty() const noexcept { return size() == 0; }

  iterator begin() noexcept { return data(); }
  const_iterator begin() const noexcept { return data(); }
  const_iterator cbegin() const noexcept { return data(); }

  iterator end() noexcept { return data() + size(); }
  const_iterator end() const noexcept { return data() + size(); }
  const_iterator cend() const noexcept { return data() + size(); }

  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }

  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

  pointer data() noexcept { return reinterpret_cast<pointer>(mmap_.data() + header_size()); }
  const_pointer data() const noexcept { return reinterpret_cast<const_pointer>(mmap_.data() + header_size()); }

  reference operator[](size_type index) noexcept { return data()[index]; }
  const_reference operator[](size_type index) const noexcept { return data()[index]; }

  reference at(size_type index) noexcept { return data()[index]; }
  const_reference at(size_type index) const noexcept { return data()[index]; }

  reference front() noexcept { return data()[0]; }
  const_reference front() const noexcept { return data()[0]; }

  reference back() noexcept { return data()[size() - 1]; }
  const_reference back() const noexcept { return data()[size() - 1]; }

  void reserve(size_type capacity);
  void resize(size_type size);
  void clear() { resize(0); }

  reference push_back(const_reference value);
  reference push_back(value_type &&value);

  template <class... Args>
  reference emplace_back(Args &&... args);

  mmap_vector &append(const_reference value);
  mmap_vector &append(value_type &&value);

  template <class It>
  mmap_vector &append(It first, It last);

private:
  mio::mmap_sink mmap_;
  path_type path_;
  size_type chunk_bytes_;

  struct header_type {
    size_type magic;
    size_type element_size;
    size_type size;
  };

  static constexpr size_type header_magic = 0x5443455650414d4d; // "MMAPVECT"

  static constexpr size_type header_size() noexcept { return sizeof(header_type); }

  header_type &header() noexcept { return *reinterpret_cast<header_type *>(mmap_.data()); }
  const header_type &header() const noexcept { return *reinterpret_cast<const header_type *>(mmap_.data()); }

  bool validate_header() const noexcept;
};

#include "details/mmap_vector.ipp"
