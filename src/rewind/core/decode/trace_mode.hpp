#pragma once

#include <cctype>
#include <string>
#include <string_view>

#include "rewind/core/decode/instruction_decoder.hpp"
#include "w1rewind/replay/replay_context.hpp"

namespace binja::rewind::core::decode {

inline bool name_has_thumb(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  std::string lower;
  lower.reserve(name.size());
  for (unsigned char ch : name) {
    lower.push_back(static_cast<char>(std::tolower(ch)));
  }
  return lower.find("thumb") != std::string::npos;
}

inline InstructionDecoder::instruction_mode instruction_mode_from_step(
    const w1::rewind::replay_context& context, const w1::rewind::flow_step& step
) {
  InstructionDecoder::instruction_mode mode{};
  if (!context.arch.has_value()) {
    return mode;
  }
  const auto& arch = *context.arch;
  for (const auto& entry : arch.modes) {
    if (entry.mode_id != step.mode_id) {
      continue;
    }
    mode.mode_valid = true;
    if (name_has_thumb(entry.name)) {
      mode.thumb = true;
    }
    return mode;
  }
  return mode;
}

} // namespace binja::rewind::core::decode
