#pragma once
#include <iterator>
#include <string>
#include <string_view>
#include "config.hpp"
#include "disk_vector.hpp"

struct StringHandle {
  using offset_type = StringHandleOffsetType;
  using length_type = StringHandleLengthType;

  offset_type offset;
  length_type length;
};

template <class Char, class Traits = std::char_traits<Char>>
class BasicStringPoolIterator {
public:
  using char_type = Char;
  using traits_type = Traits;
  using handle_type = StringHandle;
  using value_type = std::basic_string_view<char_type, traits_type>;
  using pointer = value_type *;
  using reference = value_type &;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::bidirectional_iterator_tag;

  BasicStringPoolIterator(value_type::const_pointer data, value_type::const_pointer begin = nullptr);
  handle_type handle() const noexcept;
  value_type operator*() const noexcept { return {data_, traits_type::length(data_)}; }
  BasicStringPoolIterator &operator++() noexcept;
  BasicStringPoolIterator operator++(int) noexcept;
  BasicStringPoolIterator &operator--() noexcept;
  BasicStringPoolIterator operator--(int) noexcept;
  bool operator==(const BasicStringPoolIterator &other) const noexcept { return data_ == other.data_; }
  bool operator!=(const BasicStringPoolIterator &other) const noexcept { return data_ != other.data_; }

private:
  value_type::const_pointer data_;
  value_type::const_pointer begin_;
};

template <class Char, bool NullTerminated, class Traits>
class BasicStringPoolBase {
public:
  using char_type = Char;
  using value_type = char_type;
  using traits_type = Traits;
  using container_type = DiskVector<value_type>;
  using view_type = std::basic_string_view<value_type, traits_type>;
  using handle_type = StringHandle;
  using size_type = container_type::size_type;
  using path_type = container_type::path_type;

  BasicStringPoolBase(size_type chunk_bytes = kDefaultChunkBytes) noexcept : pool_{chunk_bytes} {}
  BasicStringPoolBase(const path_type &path, OpenMode mode = kLoadOrCreate,
    size_type chunk_bytes = kDefaultChunkBytes) noexcept;

  OpenCode open(const path_type &path, OpenMode mode = kLoadOrCreate) noexcept { return pool_.open(path, mode); }
  void close() noexcept { pool_.close(); }
  void sync() noexcept { pool_.sync(); }
  bool is_open() const noexcept { return pool_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  size_type size() const noexcept { return pool_.size(); }
  size_type capacity() const noexcept { return pool_.capacity(); }
  size_type chunk_bytes() const noexcept { return pool_.chunk_bytes(); }
  void set_chunk_bytes(size_type chunk_bytes) noexcept { pool_.set_chunk_bytes(chunk_bytes); }

  void reserve(size_type capacity) { pool_.reserve(capacity); }
  void resize(size_type size) { pool_.resize(size); }
  void clear() { resize(0); }

  view_type get(size_type offset, size_type length) const noexcept { return {pool_.data() + offset, length}; }
  view_type get(handle_type handle) const noexcept { return get(handle.offset, handle.length); }
  handle_type add(view_type view);
  handle_type append(view_type view) { return add(view); }
  BasicStringPoolBase &operator+=(view_type view);

protected:
  container_type pool_;
};

template <class Char, bool NullTerminated = false, class Traits = std::char_traits<Char>>
class BasicStringPool : public BasicStringPoolBase<Char, NullTerminated, Traits> {
  using BasicStringPoolBase<Char, NullTerminated, Traits>::BasicStringPoolBase;
};

template <class Char, class Traits>
class BasicStringPool<Char, true, Traits> : public BasicStringPoolBase<Char, true, Traits> {
  using Inherited = BasicStringPoolBase<Char, true, Traits>;
  using typename Inherited::value_type;
  using typename Inherited::traits_type;
  using Inherited::BasicStringPoolBase;
  using Inherited::pool_;

public:
  using iterator = BasicStringPoolIterator<value_type, traits_type>;
  using const_iterator = iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = reverse_iterator;

  iterator begin() noexcept { return {pool_.data()}; }
  const_iterator begin() const noexcept { return {pool_.data()}; }
  const_iterator cbegin() const noexcept { return {pool_.data()}; }
  iterator end() noexcept { return {pool_.data() + pool_.size(), pool_.data()}; }
  const_iterator end() const noexcept { return {pool_.data() + pool_.size(), pool_.data()}; }
  const_iterator cend() const noexcept { return {pool_.data() + pool_.size(), pool_.data()}; }
  reverse_iterator rbegin() noexcept { return reverse_iterator{end()}; }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator{end()}; }
  const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator{end()}; }
  reverse_iterator rend() noexcept { return reverse_iterator{begin()}; }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator{begin()}; }
  const_reverse_iterator crend() const noexcept { return const_reverse_iterator{begin()}; }
};

template <bool NullTerminated = false>
using StringPool = BasicStringPool<char, NullTerminated>;


template <class Char, class Traits>
BasicStringPoolIterator<Char, Traits>::BasicStringPoolIterator(
  typename value_type::const_pointer data, typename value_type::const_pointer begin)
  : data_{data}, begin_{begin ? begin : data} {}

template <class Char, class Traits>
auto BasicStringPoolIterator<Char, Traits>::handle() const noexcept -> handle_type {
  handle_type handle;
  handle.offset = data_ - begin_;
  handle.length = traits_type::length(data_);
  return handle;
}

template <class Char, class Traits>
auto BasicStringPoolIterator<Char, Traits>::operator++() noexcept -> BasicStringPoolIterator & {
  data_ += traits_type::length(data_) + 1;
  return *this;
}

template <class Char, class Traits>
auto BasicStringPoolIterator<Char, Traits>::operator++(int) noexcept -> BasicStringPoolIterator {
  BasicStringPoolIterator temp = *this;
  ++*this;
  return temp;
}

template <class Char, class Traits>
auto BasicStringPoolIterator<Char, Traits>::operator--() noexcept -> BasicStringPoolIterator & {
  if (!begin_ || data_ == begin_) return *this;
  auto p = data_ - 1;
  while (p > begin_ && *(p - 1) != '\0') --p;
  data_ = p;
  return *this;
}

template <class Char, class Traits>
auto BasicStringPoolIterator<Char, Traits>::operator--(int) noexcept -> BasicStringPoolIterator {
  BasicStringPoolIterator temp = *this;
  --*this;
  return temp;
}

template <class Char, bool NullTerminated, class Traits>
BasicStringPoolBase<Char, NullTerminated, Traits>::BasicStringPoolBase(
  const path_type &path, OpenMode mode, size_type chunk_bytes) noexcept : pool_{path, mode, chunk_bytes} {}

template <class Char, bool NullTerminated, class Traits>
auto BasicStringPoolBase<Char, NullTerminated, Traits>::add(view_type view) -> handle_type {
  handle_type handle;
  handle.offset = pool_.size();
  handle.length = view.length();
  pool_.append(view.begin(), view.end());
  if constexpr (NullTerminated) pool_.append(static_cast<value_type>('\0'));
  return handle;
}

template <class Char, bool NullTerminated, class Traits>
auto BasicStringPoolBase<Char, NullTerminated, Traits>::operator+=(view_type view) -> BasicStringPoolBase & {
  add(view);
  return *this;
}
