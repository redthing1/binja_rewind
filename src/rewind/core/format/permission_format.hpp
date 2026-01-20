#pragma once

#include <cstdint>
#include <string>

namespace binja::rewind::core::format {

inline std::string permission_string(uint32_t perm) {
  std::string out;
  out += (perm & 1u) ? 'r' : '-';
  out += (perm & 2u) ? 'w' : '-';
  out += (perm & 4u) ? 'x' : '-';
  return out;
}

} // namespace binja::rewind::core::format
