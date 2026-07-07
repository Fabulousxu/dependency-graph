#include "data_model.hpp"

namespace xpg {

HOST_DEVICE bool isspace(char c)
  noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
HOST_DEVICE bool isdigit(char c) noexcept { return c >= '0' && c <= '9'; }
HOST_DEVICE bool isalpha(char c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
HOST_DEVICE bool isalnum(char c) noexcept { return isalpha(c) || isdigit(c); }

HOST_DEVICE std::size_t next_segment(device_string_view value, std::size_t pos) noexcept {
  while (pos < value.size() && !isalnum(value[pos]) && value[pos] != '~' && value[pos] != '^') ++pos;
  return pos;
}

HOST_DEVICE int compare_part(device_string_view left, device_string_view right) noexcept {
  std::size_t li = 0, ri = 0;
  while (li < left.size() || ri < right.size()) {
    if ((li < left.size() && left[li] == '~') || (ri < right.size() && right[ri] == '~')) {
      if (li < left.size() && left[li] == '~') { ++li; } else return 1;
      if (ri < right.size() && right[ri] == '~') { ++ri; } else return -1;
      continue;
    }
    if ((li < left.size() && left[li] == '^') || (ri < right.size() && right[ri] == '^')) {
      auto left_caret = li < left.size() && left[li] == '^';
      auto right_caret = ri < right.size() && right[ri] == '^';
      if (left_caret && right_caret) {
        ++li;
        ++ri;
        continue;
      }
      if (left_caret) return next_segment(right, ri) == right.size() ? 1 : -1;
      return next_segment(left, li) == left.size() ? -1 : 1;
    }
    li = next_segment(left, li);
    ri = next_segment(right, ri);
    if (li >= left.size() || ri >= right.size()) break;
    if (isdigit(left[li]) || isdigit(right[ri])) {
      if (!isdigit(left[li])) return -1;
      if (!isdigit(right[ri])) return 1;
      while (li < left.size() && left[li] == '0') ++li;
      while (ri < right.size() && right[ri] == '0') ++ri;
      auto lb = li;
      auto rb = ri;
      while (li < left.size() && isdigit(left[li])) ++li;
      while (ri < right.size() && isdigit(right[ri])) ++ri;
      auto llen = li - lb;
      auto rlen = ri - rb;
      if (llen != rlen) return llen < rlen ? -1 : 1;
      for (std::size_t i = 0; i < llen; ++i)
        if (left[lb + i] != right[rb + i]) return left[lb + i] < right[rb + i] ? -1 : 1;
      continue;
    }
    auto lb = li;
    auto rb = ri;
    while (li < left.size() && isalpha(left[li])) ++li;
    while (ri < right.size() && isalpha(right[ri])) ++ri;
    auto llen = li - lb;
    auto rlen = ri - rb;
    auto min_size = llen < rlen ? llen : rlen;
    for (std::size_t i = 0; i < min_size; ++i)
      if (left[lb + i] != right[rb + i]) return left[lb + i] < right[rb + i] ? -1 : 1;
    if (llen != rlen) return llen < rlen ? -1 : 1;
  }
  li = next_segment(left, li);
  ri = next_segment(right, ri);
  if (li < left.size() && left[li] == '~') return -1;
  if (ri < right.size() && right[ri] == '~') return 1;
  if (li < left.size() && left[li] == '^') return ri == right.size() ? 1 : -1;
  if (ri < right.size() && right[ri] == '^') return li == left.size() ? -1 : 1;
  if (li == left.size() && ri == right.size()) return 0;
  return li == left.size() ? -1 : 1;
}

HOST_DEVICE void split_version(device_string_view value, device_string_view &epoch, device_string_view &upstream,
                               device_string_view &revision) noexcept {
  epoch = {"0", 1};
  revision = {"0", 1};
  std::size_t begin = 0;
  if (auto pos = value.find_first_of(':'); pos != device_string_view::npos) {
    epoch = value.substr(0, pos);
    begin = pos + 1;
  }
  if (auto pos = value.find_last_of('-'); pos != device_string_view::npos && pos >= begin) {
    upstream = value.substr(begin, pos - begin);
    revision = value.substr(pos + 1, value.size() - pos - 1);
  } else upstream = value.substr(begin, value.size() - begin);
}

HOST_DEVICE int compare_version(device_string_view l, device_string_view r) noexcept {
  device_string_view lepoch, lupstream, lrevision, repoch, rupstream, rrevision;
  split_version(l, lepoch, lupstream, lrevision);
  split_version(r, repoch, rupstream, rrevision);
  if (auto epoch_cmp = compare_part(lepoch, repoch); epoch_cmp != 0) return epoch_cmp;
  if (auto upstream_cmp = compare_part(lupstream, rupstream); upstream_cmp != 0) return upstream_cmp;
  return compare_part(lrevision, rrevision);
}

bool filter_version(std::string_view target, std::string_view constraint) noexcept {
  return filter_version(device_string_view{target.data(), target.size()},
                       device_string_view{constraint.data(), constraint.size()});
}

HOST_DEVICE bool filter_version(device_string_view target, device_string_view constraint) noexcept {
  constraint.trim();
  if (constraint.empty()) return true;
  enum { kEq, kLt, kLe, kGt, kGe } op = kEq;
  if (constraint.starts_with("==")) {
    op = kEq;
    constraint.remove_prefix(2);
  } else if (constraint.starts_with("<<")) {
    op = kLt;
    constraint.remove_prefix(2);
  } else if (constraint.starts_with("<=")) {
    op = kLe;
    constraint.remove_prefix(2);
  } else if (constraint.starts_with(">>")) {
    op = kGt;
    constraint.remove_prefix(2);
  } else if (constraint.starts_with(">=")) {
    op = kGe;
    constraint.remove_prefix(2);
  } else if (constraint.starts_with('=')) {
    op = kEq;
    constraint.remove_prefix(1);
  } else if (constraint.starts_with('<')) {
    op = kLt;
    constraint.remove_prefix(1);
  } else if (constraint.starts_with('>')) {
    op = kGt;
    constraint.remove_prefix(1);
  }
  constraint.trim();
  auto cmp = compare_version(target, constraint);
  if (op == kEq) return cmp == 0;
  if (op == kGt) return cmp > 0;
  if (op == kGe) return cmp >= 0;
  if (op == kLt) return cmp < 0;
  if (op == kLe) return cmp <= 0;
  return false;
}

} // namespace xpg
