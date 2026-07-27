#include "config.hpp"

namespace xpg {

HOST_DEVICE bool satisfy_architecture(ArchitectureType target, ArchitectureType constraint,
                                      ArchitectureType source) noexcept {
  using enum ArchitectureType;
  if (constraint == kAny) return true;
  if (constraint == kAll || constraint == kNoarch) return target == kAll || target == kNoarch || target == source;
  return target == constraint;
}

bool satisfy_architecture(std::string_view target, std::string_view constraint, std::string_view source) noexcept {
  using enum ArchitectureType;
  if (constraint == kArchitectureTypes[static_cast<std::size_t>(kAny)]) return true;
  if (constraint == kArchitectureTypes[static_cast<std::size_t>(kAll)] ||
    constraint == kArchitectureTypes[static_cast<std::size_t>(kNoarch)])
    return target == kArchitectureTypes[static_cast<std::size_t>(kAll)] ||
      target == kArchitectureTypes[static_cast<std::size_t>(kNoarch)] || target == source;
  return target == constraint;
}

HOST_DEVICE bool isspace(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
HOST_DEVICE bool isdigit(char c) noexcept { return c >= '0' && c <= '9'; }
HOST_DEVICE bool isalpha(char c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
HOST_DEVICE bool isalnum(char c) noexcept { return isalpha(c) || isdigit(c); }

HOST_DEVICE std::size_t next_segment(device_string_view value, std::size_t pos) noexcept {
  while (pos < value.size() && !isalnum(value[pos]) && value[pos] != '~' && value[pos] != '^') ++pos;
  return pos;
}

HOST_DEVICE int compare_part(device_string_view l, device_string_view r) noexcept {
  auto li = 0ull, ri = 0ull;
  while (li < l.size() || ri < r.size()) {
    if ((li < l.size() && l[li] == '~') || (ri < r.size() && r[ri] == '~')) {
      if (li < l.size() && l[li] == '~') { ++li; } else return 1;
      if (ri < r.size() && r[ri] == '~') { ++ri; } else return -1;
      continue;
    }
    if ((li < l.size() && l[li] == '^') || (ri < r.size() && r[ri] == '^')) {
      auto lcaret = li < l.size() && l[li] == '^';
      auto rcaret = ri < r.size() && r[ri] == '^';
      if (lcaret && rcaret) {
        ++li;
        ++ri;
        continue;
      }
      if (lcaret) return next_segment(r, ri) == r.size() ? 1 : -1;
      return next_segment(l, li) == l.size() ? -1 : 1;
    }
    li = next_segment(l, li);
    ri = next_segment(r, ri);
    if (li >= l.size() || ri >= r.size()) break;
    if (isdigit(l[li]) || isdigit(r[ri])) {
      if (!isdigit(l[li])) return -1;
      if (!isdigit(r[ri])) return 1;
      while (li < l.size() && l[li] == '0') ++li;
      while (ri < r.size() && r[ri] == '0') ++ri;
      auto lb = li, rb = ri;
      while (li < l.size() && isdigit(l[li])) ++li;
      while (ri < r.size() && isdigit(r[ri])) ++ri;
      auto llen = li - lb, rlen = ri - rb;
      if (llen != rlen) return llen < rlen ? -1 : 1;
      for (auto i = 0ull; i < llen; ++i) if (l[lb + i] != r[rb + i]) return l[lb + i] < r[rb + i] ? -1 : 1;
      continue;
    }
    auto lb = li, rb = ri;
    while (li < l.size() && isalpha(l[li])) ++li;
    while (ri < r.size() && isalpha(r[ri])) ++ri;
    auto llen = li - lb, rlen = ri - rb;
    auto min_size = llen < rlen ? llen : rlen;
    for (auto i = 0ull; i < min_size; ++i)
      if (l[lb + i] != r[rb + i]) return l[lb + i] < r[rb + i] ? -1 : 1;
    if (llen != rlen) return llen < rlen ? -1 : 1;
  }
  li = next_segment(l, li);
  ri = next_segment(r, ri);
  if (li < l.size() && l[li] == '~') return -1;
  if (ri < r.size() && r[ri] == '~') return 1;
  if (li < l.size() && l[li] == '^') return ri == r.size() ? 1 : -1;
  if (ri < r.size() && r[ri] == '^') return li == l.size() ? -1 : 1;
  if (li == l.size() && ri == r.size()) return 0;
  return li == l.size() ? -1 : 1;
}

HOST_DEVICE void split_version(device_string_view value, device_string_view &epoch, device_string_view &upstream,
                               device_string_view &revision) noexcept {
  epoch = device_string_view{"0", 1};
  revision = device_string_view{"0", 1};
  auto begin = 0ull;
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

HOST_DEVICE bool satisfy_version(device_string_view target, device_string_view constraint) noexcept {
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
