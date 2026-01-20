#pragma once

#include <string>
#include <string_view>

namespace binja::rewind::core::mapping {

inline std::string path_basename(std::string_view path) {
  size_t end = path.size();
  while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\')) {
    --end;
  }
  size_t start = end;
  while (start > 0) {
    char c = path[start - 1];
    if (c == '/' || c == '\\') {
      break;
    }
    --start;
  }
  return std::string(path.substr(start, end - start));
}

} // namespace binja::rewind::core::mapping
