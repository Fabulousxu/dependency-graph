#pragma once
#include <cstddef>
#include <filesystem>
#include <format>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include "mmap_vector.hpp"

namespace xpg {

struct pooled_string {
  std::size_t offset;
  std::size_t length;
};

template <class CharT, class LenT = std::size_t, class Traits = std::char_traits<CharT>>
class basic_string_pool {
  class iterator_t;
  static constexpr bool has_length = !std::is_void_v<LenT>;
  using length_type = std::conditional_t<has_length, LenT, std::size_t>;

public:
  using char_type = CharT;
  using traits_type = Traits;
  using view_type = std::basic_string_view<char_type, traits_type>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using iterator = std::conditional_t<has_length, iterator_t, void>;
  using const_iterator = iterator;

  class hash {
  public:
    using is_transparent = void;

    hash(const basic_string_pool &pool) noexcept : pool_(pool) {}

    size_type operator()(size_type key) const noexcept
      requires has_length { return std::hash<view_type>()(pool_.get(key)); }
    size_type operator()(pooled_string key) const noexcept
      requires(!has_length) { return std::hash<view_type>()(pool_.get(key)); }
    size_type operator()(view_type key) const noexcept { return std::hash<view_type>()(key); }

  private:
    const basic_string_pool &pool_;
  };

  class equal_to {
  public:
    using is_transparent = void;

    equal_to(const basic_string_pool &pool) noexcept : pool_(pool) {}

    bool operator()(size_type l, size_type r) const noexcept
      requires has_length { return l == r || pool_.get(l) == pool_.get(r); }
    bool operator()(size_type l, view_type r) const noexcept requires has_length { return pool_.get(l) == r; }
    bool operator()(view_type l, size_type r) const noexcept requires has_length { return l == pool_.get(r); }
    bool operator()(pooled_string l, pooled_string r) const noexcept
      requires(!has_length) { return (l.offset == r.offset && l.length == r.length) || pool_.get(l) == pool_.get(r); }
    bool operator()(pooled_string l, view_type r) const noexcept requires(!has_length) { return pool_.get(l) == r; }
    bool operator()(view_type l, pooled_string r) const noexcept requires(!has_length) { return l == pool_.get(r); }
    bool operator()(view_type l, view_type r) const noexcept { return l == r; }

  private:
    const basic_string_pool &pool_;
  };

  basic_string_pool(size_type growth_bytes = kDefaultGrowthBytes) noexcept : pool_(growth_bytes) {}
  basic_string_pool(const std::filesystem::path &path, open_mode mode = open_mode::kLoadOrCreate,
                    size_type growth_bytes = kDefaultGrowthBytes) : pool_(growth_bytes) { open(path, mode); }
  basic_string_pool(const basic_string_pool &) = delete;
  basic_string_pool &operator=(const basic_string_pool &) = delete;
  basic_string_pool(basic_string_pool &&) noexcept = default;
  basic_string_pool &operator=(basic_string_pool &&) noexcept = default;
  ~basic_string_pool() = default;

  void load(const std::filesystem::path &path) {
    pool_.load(path);
    if (pool_.size() < sizeof(header_t) || header().magic != kMagic || header().char_size != sizeof(char_type)
      || header().length_size != (has_length ? sizeof(length_type) : 0))
      throw std::runtime_error(std::format("File '{}' is not a valid basic_string_pool.", path.string()));
  }

  void create(const std::filesystem::path &path) {
    pool_.create(path);
    pool_.resize(sizeof(header_t));
    header() = {kMagic, sizeof(char_type), has_length ? sizeof(length_type) : 0};
  }

  void open(const std::filesystem::path &path, open_mode mode = open_mode::kLoadOrCreate) {
    if (mode == open_mode::kLoad) load(path);
    else if (mode == open_mode::kCreate) create(path);
    else if (mode == open_mode::kLoadOrCreate) std::filesystem::exists(path) ? load(path) : create(path);
    else throw std::invalid_argument("Invalid open_mode.");
  }

  void close() { pool_.close(); }
  void sync() { pool_.sync(); }
  bool is_open() const noexcept { return pool_.is_open(); }
  operator bool() const noexcept { return is_open(); }

  size_type growth_bytes() const noexcept { return pool_.growth_bytes(); }
  void set_growth_bytes(size_type growth_bytes) noexcept { pool_.set_growth_bytes(growth_bytes); }
  size_type size_bytes() const noexcept { return pool_.size() - sizeof(header_t); }
  size_type capacity_bytes() const noexcept { return pool_.capacity() - sizeof(header_t); }
  bool empty() const noexcept { return size_bytes() == 0; }

  const_iterator begin() const noexcept requires has_length { return const_iterator(*this); }
  const_iterator end() const noexcept requires has_length { return const_iterator(*this, size_bytes()); }
  const_iterator cbegin() const noexcept requires has_length { return begin(); }
  const_iterator cend() const noexcept requires has_length { return end(); }

  void reserve_bytes(size_type capacity) { pool_.reserve(sizeof(header_t) + capacity); }
  void clear() { pool_.resize(sizeof(header_t)); }

  view_type get(size_type offset) const noexcept
    requires has_length { return {entry(offset).data, entry(offset).length}; }
  view_type get(size_type offset, size_type length) const noexcept
    requires(!has_length) { return {entry(offset), length}; }
  view_type get(pooled_string str) const noexcept requires(!has_length) { return get(str.offset, str.length); }
  view_type operator[](size_type offset) const noexcept requires has_length { return get(offset); }
  view_type operator[](pooled_string str) const noexcept requires(!has_length) { return get(str.offset, str.length); }

  size_type append(view_type str) requires has_length {
    auto offset = size_bytes();
    auto entry_size = sizeof(entry_t) + alignup(str.size() * sizeof(char_type), alignof(entry_t));
    pool_.resize(sizeof(header_t) + size_bytes() + entry_size);
    entry(offset).length = static_cast<length_type>(str.size());
    traits_type::copy(entry(offset).data, str.data(), str.size());
    return offset;
  }

  pooled_string append(view_type str) requires(!has_length) {
    auto offset = size_bytes();
    pool_.resize(sizeof(header_t) + size_bytes() + str.size() * sizeof(char_type));
    traits_type::copy(entry(offset), str.data(), str.size());
    return pooled_string(offset, str.size());
  }

private:
  class iterator_t {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = view_type;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = value_type;

    iterator_t(const basic_string_pool &pool, size_type offset = 0) noexcept : pool_(pool), offset_(offset) {}

    iterator_t &operator++() noexcept {
      offset_ += sizeof(entry_t) + alignup(view().size() * sizeof(char_type), alignof(entry_t));
      return *this;
    }

    iterator_t operator++(int) noexcept {
      iterator_t temp = *this;
      ++*this;
      return temp;
    }

    friend bool operator==(const iterator_t &l, const iterator_t &r) noexcept { return l.offset_ == r.offset_; }
    friend bool operator!=(const iterator_t &l, const iterator_t &r) noexcept { return !(l == r); }

    view_type view() const noexcept { return pool_.get(offset_); }
    reference operator*() const noexcept { return view(); }
    size_type offset() const noexcept { return offset_; }
    size_type length() const noexcept { return view().size(); }

  private:
    const basic_string_pool &pool_;
    size_type offset_;
  };

  struct header_t {
    size_type magic;
    size_type char_size;
    size_type length_size;
  };

  struct entry_t {
    length_type length;
    char_type data[0];
  };

  mmap_vector<std::byte> pool_;
  static constexpr size_type kMagic = 0x4c4f4f5049525453; // "STRIPOOL"

  header_t &header() noexcept { return *reinterpret_cast<header_t *>(pool_.data()); }
  const header_t &header() const noexcept { return *reinterpret_cast<const header_t *>(pool_.data()); }
  entry_t &entry(size_type offset) noexcept
    requires has_length { return *reinterpret_cast<entry_t *>(pool_.data() + sizeof(header_t) + offset); }
  const entry_t &entry(size_type offset) const noexcept
    requires has_length { return *reinterpret_cast<const entry_t *>(pool_.data() + sizeof(header_t) + offset); }
  char_type *entry(size_type offset) noexcept
    requires(!has_length) { return reinterpret_cast<char_type *>(pool_.data() + sizeof(header_t) + offset); }
  const char_type *entry(size_type offset) const noexcept
    requires(!has_length) { return reinterpret_cast<const char_type *>(pool_.data() + sizeof(header_t) + offset); }
};

template <class T, class CharT, class LenT = std::size_t, class Traits = std::char_traits<CharT>>
using basic_pooled_string_map = std::unordered_map<
  std::conditional_t<!std::is_void_v<LenT>, std::size_t, pooled_string>, T,
  typename basic_string_pool<CharT, LenT, Traits>::hash, typename basic_string_pool<CharT, LenT, Traits>::equal_to>;

template <class CharT, class LenT = std::size_t, class Traits = std::char_traits<CharT>>
using basic_pooled_string_set = std::unordered_set<
  std::conditional_t<!std::is_void_v<LenT>, std::size_t, pooled_string>,
  typename basic_string_pool<CharT, LenT, Traits>::hash, typename basic_string_pool<CharT, LenT, Traits>::equal_to>;

template <class LenT = std::size_t> using string_pool = basic_string_pool<char, LenT>;
template <class LenT = std::size_t> using wstring_pool = basic_string_pool<wchar_t, LenT>;
template <class LenT = std::size_t> using u16string_pool = basic_string_pool<char16_t, LenT>;
template <class LenT = std::size_t> using u32string_pool = basic_string_pool<char32_t, LenT>;
template <class T, class LenT = std::size_t> using pooled_string_map = basic_pooled_string_map<T, char, LenT>;
template <class T, class LenT = std::size_t> using pooled_wstring_map = basic_pooled_string_map<T, wchar_t, LenT>;
template <class T, class LenT = std::size_t> using pooled_u16string_map = basic_pooled_string_map<T, char16_t, LenT>;
template <class T, class LenT = std::size_t> using pooled_u32string_map = basic_pooled_string_map<T, char32_t, LenT>;
template <class LenT = std::size_t> using pooled_string_set = basic_pooled_string_set<char, LenT>;
template <class LenT = std::size_t> using pooled_wstring_set = basic_pooled_string_set<wchar_t, LenT>;
template <class LenT = std::size_t> using pooled_u16string_set = basic_pooled_string_set<char16_t, LenT>;
template <class LenT = std::size_t> using pooled_u32string_set = basic_pooled_string_set<char32_t, LenT>;

} // namespace xpg
