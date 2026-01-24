#pragma once
#include <chrono>
#include <cstdio>
#include <format>
#include <functional>
#include <iostream>
#include <string_view>
#include <utility>

constexpr std::string_view trim(std::string_view sv) noexcept {
  auto first = sv.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string_view::npos) return std::string_view();
  auto last = sv.find_last_not_of(" \t\n\r\f\v");
  return sv.substr(first, last - first + 1);
}

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
void print(std::format_string<Args...> fmt, Args &&... args) {
  print(std::cout, fmt, std::forward<Args>(args)...);
}

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
void println(std::format_string<Args...> fmt, Args &&... args) {
  println(std::cout, fmt, std::forward<Args>(args)...);
}

template <class Duration, class Fn, class... Args>
auto measure_time(Fn &&fn, Args &&... args) {
  using Ret = std::invoke_result_t<Fn, Args...>;
  auto start = std::chrono::high_resolution_clock::now();
  if constexpr (std::is_void_v<Ret>) {
    std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<Duration>(end - start);
  } else {
    Ret &&result = std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
    auto end = std::chrono::high_resolution_clock::now();
    return std::pair<Ret, Duration>(std::forward<Ret>(result), std::chrono::duration_cast<Duration>(end - start));
  }
}

inline std::string now_iso8601() {
  auto now = std::chrono::system_clock::now();
  auto tp = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", tp);
}
