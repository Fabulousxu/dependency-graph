#pragma once
#include <chrono>
#include <cstdio>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace xpg {

template <class CharT, class Traits = std::char_traits<CharT>>
struct basic_string_hash {
  using is_transparent = void;
  using view_type = std::basic_string_view<CharT, Traits>;
  std::size_t operator()(view_type key) const noexcept { return std::hash<view_type>()(key); }
};

template <class CharT, class Traits = std::char_traits<CharT>>
struct basic_string_equal_to {
  using is_transparent = void;
  using view_type = std::basic_string_view<CharT, Traits>;
  bool operator()(view_type l, view_type r) const noexcept { return l == r; }
};

template <class T, class CharT, class Traits = std::char_traits<CharT>, class Alloc = std::allocator<CharT>>
using basic_string_map = std::unordered_map<std::basic_string<CharT, Traits, Alloc>, T,
                                            basic_string_hash<CharT, Traits>, basic_string_equal_to<CharT, Traits>>;

template <class CharT, class Traits = std::char_traits<CharT>, class Alloc = std::allocator<CharT>>
using basic_string_set = std::unordered_set<std::basic_string<CharT, Traits, Alloc>,
                                            basic_string_hash<CharT, Traits>, basic_string_equal_to<CharT, Traits>>;

template <class T> using string_map = basic_string_map<T, char>;
template <class T> using wstring_map = basic_string_map<T, wchar_t>;
template <class T> using u16string_map = basic_string_map<T, char16_t>;
template <class T> using u32string_map = basic_string_map<T, char32_t>;
using string_set = basic_string_set<char>;
using wstring_set = basic_string_set<wchar_t>;
using u16string_set = basic_string_set<char16_t>;
using u32string_set = basic_string_set<char32_t>;

#ifdef __cpp_lib_print
#include <print>
using std::print;
using std::println;
#else

template <class... Args>
void print(FILE *stream, std::format_string<Args...> fmt, Args &&... args) {
  auto buf = std::format(fmt, std::forward<Args>(args)...);
  std::fwrite(buf.data(), 1, buf.size(), stream);
}

template <class... Args>
void print(std::ostream &os, std::format_string<Args...> fmt, Args &&... args) {
  std::ostreambuf_iterator it(os);
  std::format_to(it, fmt, std::forward<Args>(args)...);
}

template <class... Args>
void print(std::format_string<Args...> fmt, Args &&... args) { print(std::cout, fmt, std::forward<Args>(args)...); }

template <class... Args>
void println(FILE *stream, std::format_string<Args...> fmt, Args &&... args) {
  auto buf = std::format(fmt, std::forward<Args>(args)...).append('\n');
  std::fwrite(buf.data(), 1, buf.size(), stream);
}

template <class... Args>
void println(std::ostream &os, std::format_string<Args...> fmt, Args &&... args) {
  std::ostreambuf_iterator it(os);
  std::format_to(it, fmt, std::forward<Args>(args)...);
  *it = '\n';
}

template <class... Args>
void println(std::format_string<Args...> fmt, Args &&... args) { println(std::cout, fmt, std::forward<Args>(args)...); }

#endif

template <class Duration, class F, class... Args>
auto measure_time(F &&f, Args &&... args) {
  using Ret = std::invoke_result_t<F, Args...>;
  auto start = std::chrono::high_resolution_clock::now();
  if constexpr (std::is_void_v<Ret>) {
    std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<Duration>(end - start);
  } else {
    Ret &&result = std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    auto end = std::chrono::high_resolution_clock::now();
    return std::pair<Ret, Duration>(std::forward<Ret>(result), std::chrono::duration_cast<Duration>(end - start));
  }
}

inline std::string now_iso8601() {
  auto now = std::chrono::system_clock::now();
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::time_point_cast<std::chrono::milliseconds>(now));
}

} // namespace xpg
