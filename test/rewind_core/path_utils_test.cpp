#include "doctest/doctest.hpp"

#include "rewind/core/mapping/path_utils.hpp"

namespace mapping = binja::rewind::core::mapping;

TEST_CASE("path_basename handles empty and simple paths") {
  CHECK(mapping::path_basename("") == "");
  CHECK(mapping::path_basename("file") == "file");
  CHECK(mapping::path_basename("/usr/bin/ls") == "ls");
  CHECK(mapping::path_basename("/usr/bin/ls/") == "ls");
}

TEST_CASE("path_basename handles windows paths") {
  CHECK(mapping::path_basename("C:\\foo\\bar.exe") == "bar.exe");
  CHECK(mapping::path_basename("C:\\foo\\bar\\") == "bar");
}

TEST_CASE("path_basename tolerates mixed separators") {
  CHECK(mapping::path_basename("C:/foo\\bar") == "bar");
  CHECK(mapping::path_basename("/foo\\bar/") == "bar");
}
