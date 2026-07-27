#pragma once
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>

#ifndef HOST_DEVICE
#ifdef __CUDACC__
#define HOST_DEVICE __host__ __device__
#else
#define HOST_DEVICE
#endif
#endif

namespace xpg {

template <class CharT>
struct device_char_traits {
  using char_type = CharT;
  using int_type = std::char_traits<char_type>::int_type;
  using pos_type = std::char_traits<char_type>::pos_type;
  using off_type = std::char_traits<char_type>::off_type;
  using state_type = std::char_traits<char_type>::state_type;
  using comparison_category = std::strong_ordering;

  HOST_DEVICE static constexpr void assign(char_type &l, const char_type &r) noexcept { l = r; }
  HOST_DEVICE static constexpr bool eq(char_type l, char_type r) noexcept { return l == r; }
  HOST_DEVICE static constexpr bool lt(char_type l, char_type r) noexcept { return l < r; }

  HOST_DEVICE static constexpr int compare(const char_type *l, const char_type *r, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
      if (lt(l[i], r[i])) return -1;
      if (lt(r[i], l[i])) return 1;
    }
    return 0;
  }

  HOST_DEVICE static constexpr std::size_t length(const char_type *str) noexcept {
    std::size_t i = 0;
    while (!eq(str[i], char_type())) ++i;
    return i;
  }

  HOST_DEVICE static constexpr const char_type *find(const char_type *str, std::size_t n, const char_type &c) noexcept {
    for (std::size_t i = 0; i < n; ++i) if (eq(str[i], c)) return str + i;
    return nullptr;
  }

  HOST_DEVICE static constexpr char_type *move(char_type *dst, const char_type *src, std::size_t n) noexcept {
    if (n == 0 || dst == src) return dst;
    if (src < dst && dst < src + n) for (std::size_t i = n; i > 0; --i) assign(dst[i - 1], src[i - 1]);
    else copy(dst, src, n);
    return dst;
  }

  HOST_DEVICE static constexpr char_type *copy(char_type *dst, const char_type *src, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) dst[i] = src[i];
    return dst;
  }

  HOST_DEVICE static constexpr char_type *assign(char_type *dst, std::size_t n, char_type c) noexcept {
    for (std::size_t i = 0; i < n; ++i) dst[i] = c;
    return dst;
  }

  HOST_DEVICE static constexpr char_type to_char_type(const int_type &c) noexcept { return static_cast<char_type>(c); }
  HOST_DEVICE static constexpr int_type to_int_type(const char_type &c) noexcept { return static_cast<int_type>(c); }
  HOST_DEVICE static constexpr bool eq_int_type(const int_type &l, const int_type &r) noexcept { return l == r; }
  HOST_DEVICE static constexpr int_type eof() noexcept { return static_cast<int_type>(-1); }
  HOST_DEVICE static constexpr int_type not_eof(const int_type &c)
    noexcept { return eq_int_type(c, eof()) ? to_int_type(char_type()) : c; }
};

template <class CharT, class Traits = device_char_traits<CharT>>
class device_basic_string_view {
  static_assert(!std::is_array_v<CharT>);
  static_assert(std::is_trivial_v<CharT> && std::is_standard_layout_v<CharT>);
  static_assert(std::is_same_v<CharT, typename Traits::char_type>);

public:
  using traits_type = Traits;
  using value_type = CharT;
  using pointer = value_type *;
  using const_pointer = const value_type *;
  using reference = value_type &;
  using const_reference = const value_type &;
  using const_iterator = const_pointer;
  using iterator = const_iterator;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reverse_iterator = const_reverse_iterator;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  static constexpr size_type npos = static_cast<size_type>(-1);

  HOST_DEVICE constexpr device_basic_string_view() noexcept : data_(nullptr), size_(0) {}
  HOST_DEVICE constexpr device_basic_string_view(const device_basic_string_view &other) noexcept
    : data_(other.data_), size_(other.size_) {}

  HOST_DEVICE constexpr device_basic_string_view &operator=(const device_basic_string_view &other) noexcept {
    data_ = other.data_;
    size_ = other.size_;
    return *this;
  }

  HOST_DEVICE constexpr device_basic_string_view(const_pointer str) noexcept
    : data_(str), size_(traits_type::length(str)) {}
  HOST_DEVICE constexpr device_basic_string_view(const_pointer str, size_type len) noexcept : data_(str), size_(len) {}

  template <std::contiguous_iterator It, std::sized_sentinel_for<It> End>
    requires std::same_as<std::iter_value_t<It>, value_type> && (!std::convertible_to<End, size_type>)
  HOST_DEVICE constexpr device_basic_string_view(It first, End last) noexcept
    : data_(std::to_address(first)), size_(last - first) {}

  template <std::ranges::contiguous_range R>
    requires (!std::same_as<std::decay_t<R>, device_basic_string_view>) && std::ranges::sized_range<R>
    && std::same_as<std::ranges::range_value_t<R>, value_type> && (!std::is_convertible_v<R, const_pointer>)
  HOST_DEVICE constexpr device_basic_string_view(R &&r) noexcept
    : data_(std::ranges::data(r)), size_(std::ranges::size(r)) {}

  HOST_DEVICE constexpr const_iterator begin() const noexcept { return data_; }
  HOST_DEVICE constexpr const_iterator end() const noexcept { return data_ + size_; }
  HOST_DEVICE constexpr const_iterator cbegin() const noexcept { return begin(); }
  HOST_DEVICE constexpr const_iterator cend() const noexcept { return end(); }
  HOST_DEVICE constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  HOST_DEVICE constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  HOST_DEVICE constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  HOST_DEVICE constexpr const_reverse_iterator crend() const noexcept { return rend(); }

  HOST_DEVICE constexpr size_type size() const noexcept { return size_; }
  HOST_DEVICE constexpr size_type length() const noexcept { return size_; }
  HOST_DEVICE constexpr size_type max_size() const
    noexcept { return (npos - sizeof(size_type) - sizeof(void *)) / sizeof(value_type) / 4; }
  HOST_DEVICE constexpr bool empty() const noexcept { return size_ == 0; }

  HOST_DEVICE constexpr const_pointer data() const noexcept { return data_; }
  HOST_DEVICE constexpr const_reference operator[](size_type pos) const noexcept { return data_[pos]; }
  HOST_DEVICE constexpr const_reference at(size_type pos) const noexcept { return data_[pos]; }
  HOST_DEVICE constexpr const_reference front() const noexcept { return data_[0]; }
  HOST_DEVICE constexpr const_reference back() const noexcept { return data_[size_ - 1]; }

  HOST_DEVICE constexpr void remove_prefix(size_type n) noexcept { data_ += n, size_ -= n; }
  HOST_DEVICE constexpr void remove_suffix(size_type n) noexcept { size_ -= n; }

  HOST_DEVICE constexpr void trim_prefix() noexcept {
    auto first = find_first_not_of(" \t\n\r\f\v");
    remove_prefix(first == npos ? size_ : first);
  }

  HOST_DEVICE constexpr void trim_suffix() noexcept {
    auto last = find_last_not_of(" \t\n\r\f\v");
    remove_suffix(size_ - (last == npos ? 0 : last + 1));
  }

  HOST_DEVICE constexpr void trim() noexcept {
    trim_prefix();
    trim_suffix();
  }

  HOST_DEVICE constexpr void swap(device_basic_string_view &other) noexcept {
    auto tmp = *this;
    *this = other;
    other = tmp;
  }

  HOST_DEVICE constexpr size_type copy(CharT *dst, size_type n, size_type pos = 0) const noexcept {
    if (pos > size_) return 0;
    auto len = n < size_ - pos ? n : size_ - pos;
    traits_type::copy(dst, data_ + pos, len);
    return len;
  }

  HOST_DEVICE constexpr device_basic_string_view substr(size_type pos = 0, size_type n = npos) const noexcept {
    if (pos > size_) pos = size_;
    auto len = n < size_ - pos ? n : size_ - pos;
    return {data_ + pos, len};
  }

  HOST_DEVICE constexpr int compare(device_basic_string_view sv) const noexcept {
    auto len = size_ < sv.size_ ? size_ : sv.size_;
    if (auto cmp = traits_type::compare(data_, sv.data_, len); cmp != 0) return cmp;
    if (size_ == sv.size_) return 0;
    return size_ < sv.size_ ? -1 : 1;
  }

  HOST_DEVICE constexpr int compare(size_type pos1, size_type n1, device_basic_string_view sv) const
    noexcept { return substr(pos1, n1).compare(sv); }
  HOST_DEVICE constexpr int compare(size_type pos1, size_type n1, device_basic_string_view sv, size_type pos2,
                                    size_type n2 = npos) const
    noexcept { return substr(pos1, n1).compare(sv.substr(pos2, n2)); }
  HOST_DEVICE constexpr int compare(const_pointer str) const noexcept { return compare(device_basic_string_view(str)); }
  HOST_DEVICE constexpr int compare(size_type pos1, size_type n1, const_pointer str) const
    noexcept { return substr(pos1, n1).compare(device_basic_string_view(str)); }
  HOST_DEVICE constexpr int compare(size_type pos1, size_type n1, const_pointer str, size_type n2) const
    noexcept { return substr(pos1, n1).compare(device_basic_string_view(str, n2)); }

  HOST_DEVICE constexpr bool starts_with(device_basic_string_view sv) const
    noexcept { return size_ >= sv.size_ && substr(0, sv.size_) == sv; }
  HOST_DEVICE constexpr bool starts_with(value_type c) const
    noexcept { return !empty() && traits_type::eq(front(), c); }
  HOST_DEVICE constexpr bool starts_with(const_pointer str) const
    noexcept { return starts_with(device_basic_string_view(str)); }
  HOST_DEVICE constexpr bool ends_with(device_basic_string_view sv) const
    noexcept { return size_ >= sv.size_ && substr(size_ - sv.size_, sv.size_) == sv; }
  HOST_DEVICE constexpr bool ends_with(value_type c) const
    noexcept { return !empty() && traits_type::eq(back(), c); }
  HOST_DEVICE constexpr bool ends_with(const_pointer str) const
    noexcept { return ends_with(device_basic_string_view(str)); }

  HOST_DEVICE constexpr bool contains(device_basic_string_view sv) const noexcept { return find(sv) != npos; }
  HOST_DEVICE constexpr bool contains(value_type c) const noexcept { return find(c) != npos; }
  HOST_DEVICE constexpr bool contains(const_pointer str) const noexcept { return find(str) != npos; }

  HOST_DEVICE constexpr size_type find(device_basic_string_view sv, size_type pos = 0) const noexcept {
    if (pos > size_ || sv.size_ > size_ - pos) return npos;
    if (sv.size_ == 0) return pos;
    for (size_type i = pos; i <= size_ - sv.size_; ++i)
      if (traits_type::compare(data_ + i, sv.data_, sv.size_) == 0) return i;
    return npos;
  }

  HOST_DEVICE constexpr size_type find(value_type c, size_type pos = 0) const noexcept {
    if (pos >= size_) return npos;
    const auto ptr = traits_type::find(data_ + pos, size_ - pos, c);
    return ptr == nullptr ? npos : static_cast<size_type>(ptr - data_);
  }

  HOST_DEVICE constexpr size_type find(const_pointer str, size_type pos, size_type n) const
    noexcept { return find(device_basic_string_view(str, n), pos); }
  HOST_DEVICE constexpr size_type find(const_pointer str, size_type pos = 0) const
    noexcept { return find(device_basic_string_view(str), pos); }

  HOST_DEVICE constexpr size_type rfind(device_basic_string_view sv, size_type pos = npos) const noexcept {
    if (sv.size_ > size_) return npos;
    for (auto i = pos < size_ - sv.size_ ? pos : size_ - sv.size_; i > 0; --i)
      if (traits_type::compare(data_ + i, sv.data_, sv.size_) == 0) return i;
    return npos;
  }

  HOST_DEVICE constexpr size_type rfind(value_type c, size_type pos = npos) const noexcept {
    if (empty()) return npos;
    for (auto i = pos < size_ - 1 ? pos : size_ - 1; i > 0; --i)
      if (traits_type::eq(data_[i], c)) return i;
    return npos;
  }

  HOST_DEVICE constexpr size_type rfind(const_pointer str, size_type pos, size_type n) const
    noexcept { return rfind(device_basic_string_view(str, n), pos); }
  HOST_DEVICE constexpr size_type rfind(const_pointer str, size_type pos = npos) const
    noexcept { return rfind(device_basic_string_view(str), pos); }

  HOST_DEVICE constexpr size_type find_first_of(device_basic_string_view sv, size_type pos = 0) const noexcept {
    for (size_type i = pos; i < size_; ++i) if (sv.find(data_[i]) != npos) return i;
    return npos;
  }

  HOST_DEVICE constexpr size_type find_first_of(value_type c, size_type pos = 0) const noexcept { return find(c, pos); }
  HOST_DEVICE constexpr size_type find_first_of(const_pointer str, size_type pos, size_type n) const
    noexcept { return find_first_of(device_basic_string_view(str, n), pos); }
  HOST_DEVICE constexpr size_type find_first_of(const_pointer str, size_type pos = 0) const
    noexcept { return find_first_of(device_basic_string_view(str), pos); }

  HOST_DEVICE constexpr size_type find_last_of(device_basic_string_view sv, size_type pos = npos) const noexcept {
    if (empty()) return npos;
    for (size_type i = pos < size_ - 1 ? pos : size_ - 1; i > 0; --i) if (sv.find(data_[i]) != npos) return i;
    return npos;
  }

  HOST_DEVICE constexpr size_type find_last_of(value_type c, size_type pos = npos) const
    noexcept { return rfind(c, pos); }
  HOST_DEVICE constexpr size_type find_last_of(const_pointer str, size_type pos, size_type n) const
    noexcept { return find_last_of(device_basic_string_view(str, n), pos); }
  HOST_DEVICE constexpr size_type find_last_of(const_pointer str, size_type pos = npos) const
    noexcept { return find_last_of(device_basic_string_view(str), pos); }

  HOST_DEVICE constexpr size_type find_first_not_of(device_basic_string_view sv, size_type pos = 0) const noexcept {
    for (size_type i = pos; i < size_; ++i) if (sv.find(data_[i]) == npos) return i;
    return npos;
  }

  HOST_DEVICE constexpr size_type find_first_not_of(value_type c, size_type pos = 0) const noexcept {
    for (size_type i = pos; i < size_; ++i) if (!traits_type::eq(data_[i], c)) return i;
    return npos;
  }

  HOST_DEVICE constexpr size_type find_first_not_of(const_pointer str, size_type pos, size_type n) const
    noexcept { return find_first_not_of(device_basic_string_view(str, n), pos); }
  HOST_DEVICE constexpr size_type find_first_not_of(const_pointer str, size_type pos = 0) const
    noexcept { return find_first_not_of(device_basic_string_view(str), pos); }

  HOST_DEVICE constexpr size_type find_last_not_of(device_basic_string_view sv, size_type pos = npos) const noexcept {
    if (empty()) return npos;
    for (size_type i = pos < size_ - 1 ? pos : size_ - 1; i > 0; --i) if (sv.find(data_[i]) == npos) return i;
    return npos;
  }

  HOST_DEVICE constexpr size_type find_last_not_of(value_type c, size_type pos = npos) const noexcept {
    if (empty()) return npos;
    for (size_type i = pos < size_ - 1 ? pos : size_ - 1; i > 0; --i) if (!traits_type::eq(data_[i], c)) return i;
    return npos;
  }

  HOST_DEVICE constexpr size_type find_last_not_of(const_pointer str, size_type pos, size_type n) const
    noexcept { return find_last_not_of(device_basic_string_view(str, n), pos); }
  HOST_DEVICE constexpr size_type find_last_not_of(const_pointer str, size_type pos = npos) const
    noexcept { return find_last_not_of(device_basic_string_view(str), pos); }

  constexpr operator std::basic_string_view<CharT>() const noexcept { return {data_, size_}; }

  friend HOST_DEVICE constexpr bool operator==(device_basic_string_view l, device_basic_string_view r)
    noexcept { return l.compare(r) == 0; }
  friend HOST_DEVICE constexpr bool operator!=(device_basic_string_view l, device_basic_string_view r)
    noexcept { return !(l == r); }

  friend HOST_DEVICE constexpr traits_type::comparison_category
  operator<=>(device_basic_string_view l, device_basic_string_view r) noexcept {
    auto cmp = l.compare(r);
    if (cmp < 0) return traits_type::comparison_category::less;
    if (cmp > 0) return traits_type::comparison_category::greater;
    return traits_type::comparison_category::equivalent;
  }

private:
  const_pointer data_ = nullptr;
  size_type size_ = 0;
};

using device_string_view = device_basic_string_view<char>;
using device_wstring_view = device_basic_string_view<wchar_t>;
using device_u8string_view = device_basic_string_view<char8_t>;
using device_u16string_view = device_basic_string_view<char16_t>;
using device_u32string_view = device_basic_string_view<char32_t>;

} // namespace xpg

template <class CharT, class Traits>
struct std::hash<xpg::device_basic_string_view<CharT, Traits>> {
  HOST_DEVICE constexpr std::size_t operator()(xpg::device_basic_string_view<CharT, Traits> sv) const noexcept {
    std::size_t seed = sizeof(std::size_t) == 8 ? 0xcbf29ce484222325ull : 0x811c9dc5u;
    for (std::size_t i = 0; i < sv.size(); ++i) {
      seed ^= static_cast<std::size_t>(Traits::to_int_type(sv[i]));
      seed *= sizeof(std::size_t) == 8 ? 0x100000001b3ull : 0x01000193u;
    }
    return seed;
  }
};
