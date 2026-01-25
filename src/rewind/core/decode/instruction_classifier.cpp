#include "rewind/core/decode/instruction_classifier.hpp"

#include <array>
#include <cctype>
#include <string>

namespace binja::rewind::core::decode {
namespace {

enum class arch_family { unknown, x86, arm32, arm64, mips, ppc, riscv };

std::string normalize_mnemonic(std::string_view mnemonic) {
  std::string out;
  out.reserve(mnemonic.size());
  for (char c : mnemonic) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) || c == '_') {
      out.push_back(static_cast<char>(std::tolower(uc)));
    }
  }
  return out;
}

arch_family classify_arch(std::string_view arch_name) {
  std::string name = normalize_mnemonic(arch_name);
  if (name.find("x86") != std::string::npos || name.find("amd64") != std::string::npos) {
    return arch_family::x86;
  }
  if (name.find("aarch64") != std::string::npos || name.find("arm64") != std::string::npos) {
    return arch_family::arm64;
  }
  if (name.find("armv7") != std::string::npos || name.find("thumb") != std::string::npos ||
      (name.find("arm") != std::string::npos && name.find("64") == std::string::npos)) {
    return arch_family::arm32;
  }
  if (name.find("mips") != std::string::npos) {
    return arch_family::mips;
  }
  if (name.find("ppc") != std::string::npos || name.find("powerpc") != std::string::npos) {
    return arch_family::ppc;
  }
  if (name.find("riscv") != std::string::npos) {
    return arch_family::riscv;
  }
  return arch_family::unknown;
}

bool is_arm_condition(std::string_view suffix) {
  static constexpr std::array<std::string_view, 16> k_conditions = {"eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl",
                                                                    "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"};
  if (suffix.empty()) {
    return false;
  }
  for (auto cond : k_conditions) {
    if (suffix == cond) {
      return true;
    }
  }
  return false;
}

bool is_call_mnemonic_for_arch(std::string_view mnemonic, arch_family family) {
  if (mnemonic.empty()) {
    return false;
  }
  switch (family) {
  case arch_family::x86:
    return mnemonic.starts_with("call") || mnemonic == "lcall";
  case arch_family::arm32: {
    if (mnemonic == "bl" || mnemonic == "blx") {
      return true;
    }
    if (mnemonic.starts_with("blx")) {
      auto suffix = mnemonic.substr(3);
      return suffix.empty() || is_arm_condition(suffix);
    }
    if (mnemonic.starts_with("bl")) {
      auto suffix = mnemonic.substr(2);
      return suffix.empty() || is_arm_condition(suffix);
    }
    return false;
  }
  case arch_family::arm64:
    if (mnemonic == "bl") {
      return true;
    }
    return mnemonic.starts_with("blr");
  case arch_family::mips:
    return mnemonic == "jal" || mnemonic == "jalr" || mnemonic == "jalx" || mnemonic == "bal";
  case arch_family::ppc:
    return mnemonic == "bl" || mnemonic == "bctrl" || mnemonic == "bcl";
  case arch_family::riscv:
    return mnemonic == "jal" || mnemonic == "jalr";
  case arch_family::unknown:
    return false;
  }
  return false;
}

bool is_return_mnemonic_for_arch(std::string_view mnemonic, arch_family family) {
  if (mnemonic.empty()) {
    return false;
  }
  switch (family) {
  case arch_family::x86:
    return mnemonic.starts_with("ret") || mnemonic.starts_with("iret") || mnemonic.starts_with("sysret");
  case arch_family::arm64:
    return mnemonic == "ret";
  case arch_family::arm32:
  case arch_family::mips:
  case arch_family::ppc:
  case arch_family::riscv:
  case arch_family::unknown:
    return false;
  }
  return false;
}

} // namespace

bool is_call_mnemonic(std::string_view mnemonic, std::string_view arch_name) {
  auto normalized = normalize_mnemonic(mnemonic);
  auto family = classify_arch(arch_name);
  return is_call_mnemonic_for_arch(normalized, family);
}

bool is_return_mnemonic(std::string_view mnemonic, std::string_view arch_name) {
  auto normalized = normalize_mnemonic(mnemonic);
  auto family = classify_arch(arch_name);
  return is_return_mnemonic_for_arch(normalized, family);
}

} // namespace binja::rewind::core::decode
