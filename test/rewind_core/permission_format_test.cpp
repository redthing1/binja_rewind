#include "doctest/doctest.hpp"

#include "rewind/core/format/permission_format.hpp"

namespace fmt = binja::rewind::core::format;

TEST_CASE("permission_string formats rwx bits") {
  CHECK(fmt::permission_string(0) == "---");
  CHECK(fmt::permission_string(1) == "r--");
  CHECK(fmt::permission_string(2) == "-w-");
  CHECK(fmt::permission_string(3) == "rw-");
  CHECK(fmt::permission_string(4) == "--x");
  CHECK(fmt::permission_string(5) == "r-x");
  CHECK(fmt::permission_string(6) == "-wx");
  CHECK(fmt::permission_string(7) == "rwx");
}
