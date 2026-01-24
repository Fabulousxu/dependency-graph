#pragma once

template <class Char, class Traits>
basic_string_pool_iterator<Char, Traits>::basic_string_pool_iterator(const char_type *data, const char_type *begin)
  noexcept : data_(data), begin_(begin ? begin : data) {}

template <class Char, class Traits>
auto basic_string_pool_iterator<Char, Traits>::operator++() noexcept -> basic_string_pool_iterator & {
  data_ += traits_type::length(data_) + 1;
  return *this;
}

template <class Char, class Traits>
auto basic_string_pool_iterator<Char, Traits>::operator++(int) noexcept -> basic_string_pool_iterator {
  auto temp = *this;
  ++*this;
  return temp;
}

template <class Char, class Traits>
auto basic_string_pool_iterator<Char, Traits>::operator--() noexcept -> basic_string_pool_iterator & {
  if (!begin_ || data_ == begin_) return *this;
  --data_;
  while (data_ > begin_ && *(data_ - 1) != null) --data_;
  return *this;
}

template <class Char, class Traits>
auto basic_string_pool_iterator<Char, Traits>::operator--(int) noexcept -> basic_string_pool_iterator {
  auto temp = *this;
  --*this;
  return temp;
}

template <class Char, class Traits>
auto basic_string_pool_iterator<Char, Traits>::handle() const noexcept -> handle_type {
  return {
    .offset = static_cast<handle_type::offset_type>(data_ - begin_),
    .length = static_cast<handle_type::length_type>(traits_type::length(data_))
  };
}

template <class Char, bool NullTerminated, class Traits>
basic_string_pool_base<Char, NullTerminated, Traits>::basic_string_pool_base(
  const path_type &path, open_mode mode, size_type chunk_bytes) noexcept : pool_(path, mode, chunk_bytes) {}

template <class Char, bool NullTerminated, class Traits>
open_code basic_string_pool_base<Char, NullTerminated, Traits>::open(const path_type &path, open_mode mode) noexcept {
  return pool_.open(path, mode);
}

template <class Char, bool NullTerminated, class Traits>
auto basic_string_pool_base<Char, NullTerminated, Traits>::get(
  handle_type::offset_type offset, handle_type::length_type length) const noexcept -> view_type {
  return view_type(pool_.data() + offset, length);
}

template <class Char, bool NullTerminated, class Traits>
auto basic_string_pool_base<Char, NullTerminated, Traits>::add(view_type view) -> handle_type {
  handle_type handle{
    .offset = static_cast<handle_type::offset_type>(pool_.size()),
    .length = static_cast<handle_type::length_type>(view.length())
  };
  pool_.append(view.begin(), view.end());
  if constexpr (NullTerminated) pool_.append(null);
  return handle;
}

template <class Char, bool NullTerminated, class Traits>
auto basic_string_pool_base<Char, NullTerminated, Traits>::operator+=(view_type view) -> basic_string_pool_base & {
  add(view);
  return *this;
}
