#include "doctest/doctest.hpp"

#include "rewind/core/decode/instruction_classifier.hpp"

TEST_CASE("instruction classifier detects x86 calls") {
  CHECK(binja::rewind::core::decode::is_call_mnemonic("call", "x86_64"));
  CHECK(binja::rewind::core::decode::is_call_mnemonic("CALLQ", "x86"));
  CHECK(binja::rewind::core::decode::is_call_mnemonic("lcall", "x86_64"));
  CHECK_FALSE(binja::rewind::core::decode::is_call_mnemonic("jmp", "x86_64"));
}

TEST_CASE("instruction classifier detects arm calls") {
  CHECK(binja::rewind::core::decode::is_call_mnemonic("bl", "armv7"));
  CHECK(binja::rewind::core::decode::is_call_mnemonic("blne", "armv7"));
  CHECK(binja::rewind::core::decode::is_call_mnemonic("blx", "thumb2"));
  CHECK_FALSE(binja::rewind::core::decode::is_call_mnemonic("blt", "armv7"));

  CHECK(binja::rewind::core::decode::is_call_mnemonic("bl", "aarch64"));
  CHECK(binja::rewind::core::decode::is_call_mnemonic("blr", "arm64"));
  CHECK(binja::rewind::core::decode::is_call_mnemonic("blraa", "aarch64"));
  CHECK_FALSE(binja::rewind::core::decode::is_call_mnemonic("b.lt", "aarch64"));
}

TEST_CASE("instruction classifier detects returns") {
  CHECK(binja::rewind::core::decode::is_return_mnemonic("ret", "x86_64"));
  CHECK(binja::rewind::core::decode::is_return_mnemonic("iretq", "x86_64"));
  CHECK(binja::rewind::core::decode::is_return_mnemonic("sysret", "x86_64"));
  CHECK(binja::rewind::core::decode::is_return_mnemonic("ret", "aarch64"));
  CHECK_FALSE(binja::rewind::core::decode::is_return_mnemonic("br", "aarch64"));
}

TEST_CASE("instruction classifier does not guess on unknown arches") {
  CHECK_FALSE(binja::rewind::core::decode::is_call_mnemonic("call", "mystery"));
  CHECK_FALSE(binja::rewind::core::decode::is_return_mnemonic("ret", "mystery"));
}
