#include "rewind/core/engine/breakpoint_matcher.hpp"

namespace binja::rewind::core::engine {

bool breakpoint_matcher::is_skipped(const std::optional<breakpoint_skip>& skip, uint64_t sequence, uint64_t address) {
  return skip.has_value() && skip->sequence == sequence && skip->address == address;
}

breakpoint_match breakpoint_matcher::match_in_current_block(
    const w1::rewind::replay_context& context, const w1::rewind::flow_step& current_step, bool forward,
    const std::unordered_set<uint64_t>& breakpoints, const std::optional<breakpoint_skip>& skip, std::string& error
) const {
  breakpoint_match result{};
  error.clear();
  if (breakpoints.empty()) {
    return result;
  }
  if (!context.has_blocks()) {
    return result;
  }
  if (current_step.block_id == 0) {
    return result;
  }

  auto it = context.blocks_by_id.find(current_step.block_id);
  if (it == context.blocks_by_id.end()) {
    return result;
  }

  w1::rewind::flow_step block_step = current_step;
  block_step.is_block = true;
  block_step.address = it->second.address;
  block_step.size = it->second.size;

  if (block_step.size == 0) {
    return result;
  }

  bool candidate = false;
  uint64_t block_start = block_step.address;
  uint64_t block_end = block_start + block_step.size;
  for (const auto& bp : breakpoints) {
    if (bp >= block_start && bp < block_end && !is_skipped(skip, current_step.sequence, bp)) {
      candidate = true;
      break;
    }
  }
  if (!candidate) {
    return result;
  }

  if (!decoder_) {
    error = "block decoder unavailable";
    result.kind = breakpoint_match_kind::unresolved_block;
    result.address = block_step.address;
    return result;
  }

  w1::rewind::decoded_block decoded{};
  if (!decoder_->decode_block(context, block_step, decoded, error)) {
    if (error.empty()) {
      error = "block decode failed";
    }
    result.kind = breakpoint_match_kind::unresolved_block;
    result.address = block_step.address;
    return result;
  }

  size_t current_index = decoded.instructions.size();
  if (current_step.is_block) {
    current_index = forward ? 0 : decoded.instructions.size();
  } else {
    for (size_t i = 0; i < decoded.instructions.size(); ++i) {
      if (decoded.instructions[i].address == current_step.address) {
        current_index = i;
        break;
      }
    }
    if (current_index == decoded.instructions.size()) {
      return result;
    }
  }

  if (forward) {
    size_t start = current_step.is_block ? current_index : current_index + 1;
    for (size_t i = start; i < decoded.instructions.size(); ++i) {
      uint64_t addr = decoded.instructions[i].address;
      if (breakpoints.find(addr) != breakpoints.end() && !is_skipped(skip, current_step.sequence, addr)) {
        result.kind = breakpoint_match_kind::exact;
        result.address = addr;
        return result;
      }
    }
  } else {
    size_t start = current_index;
    for (size_t i = start; i-- > 0;) {
      uint64_t addr = decoded.instructions[i].address;
      if (breakpoints.find(addr) != breakpoints.end() && !is_skipped(skip, current_step.sequence, addr)) {
        result.kind = breakpoint_match_kind::exact;
        result.address = addr;
        return result;
      }
    }
  }

  error = "breakpoint address not found in decoded block";
  result.kind = breakpoint_match_kind::unresolved_block;
  result.address = block_step.address;
  return result;
}

breakpoint_match breakpoint_matcher::match_step(
    const w1::rewind::replay_context& context, const w1::rewind::flow_step& step, bool forward,
    const std::unordered_set<uint64_t>& breakpoints, const std::optional<breakpoint_skip>& skip, std::string& error
) const {
  breakpoint_match result{};
  error.clear();
  if (breakpoints.empty()) {
    return result;
  }

  if (!context.has_blocks()) {
    if (breakpoints.find(step.address) != breakpoints.end() && !is_skipped(skip, step.sequence, step.address)) {
      result.kind = breakpoint_match_kind::exact;
      result.address = step.address;
    }
    return result;
  }

  if (breakpoints.find(step.address) != breakpoints.end() && !is_skipped(skip, step.sequence, step.address)) {
    result.kind = breakpoint_match_kind::exact;
    result.address = step.address;
    return result;
  }

  if (step.size == 0) {
    return result;
  }

  bool candidate = false;
  uint64_t block_start = step.address;
  uint64_t block_end = block_start + step.size;
  for (const auto& bp : breakpoints) {
    if (bp >= block_start && bp < block_end && !is_skipped(skip, step.sequence, bp)) {
      candidate = true;
      break;
    }
  }
  if (!candidate) {
    return result;
  }

  if (!decoder_) {
    error = "block decoder unavailable";
    result.kind = breakpoint_match_kind::unresolved_block;
    result.address = step.address;
    return result;
  }

  w1::rewind::decoded_block decoded{};
  if (!decoder_->decode_block(context, step, decoded, error)) {
    if (error.empty()) {
      error = "block decode failed";
    }
    result.kind = breakpoint_match_kind::unresolved_block;
    result.address = step.address;
    return result;
  }

  if (forward) {
    for (const auto& inst : decoded.instructions) {
      uint64_t addr = inst.address;
      if (breakpoints.find(addr) != breakpoints.end() && !is_skipped(skip, step.sequence, addr)) {
        result.kind = breakpoint_match_kind::exact;
        result.address = addr;
        return result;
      }
    }
  } else {
    for (auto it = decoded.instructions.rbegin(); it != decoded.instructions.rend(); ++it) {
      uint64_t addr = it->address;
      if (breakpoints.find(addr) != breakpoints.end() && !is_skipped(skip, step.sequence, addr)) {
        result.kind = breakpoint_match_kind::exact;
        result.address = addr;
        return result;
      }
    }
  }

  error = "breakpoint address not found in decoded block";
  result.kind = breakpoint_match_kind::unresolved_block;
  result.address = step.address;
  return result;
}

} // namespace binja::rewind::core::engine
