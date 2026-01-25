#pragma once

#include <optional>
#include <string>
#include <unordered_set>

#include "w1rewind/replay/block_decoder.hpp"
#include "w1rewind/replay/replay_context.hpp"

namespace binja::rewind::core::engine {

struct breakpoint_skip {
  uint64_t address = 0;
  uint64_t sequence = 0;
};

enum class breakpoint_match_kind { none, exact, unresolved_block };

struct breakpoint_match {
  breakpoint_match_kind kind = breakpoint_match_kind::none;
  uint64_t address = 0;
};

class breakpoint_matcher {
public:
  explicit breakpoint_matcher(w1::rewind::block_decoder* decoder) : decoder_(decoder) {}

  breakpoint_match match_in_current_block(
      const w1::rewind::replay_context& context, const w1::rewind::flow_step& current_step, bool forward,
      const std::unordered_set<uint64_t>& breakpoints, const std::optional<breakpoint_skip>& skip, std::string& error
  ) const;

  breakpoint_match match_step(
      const w1::rewind::replay_context& context, const w1::rewind::flow_step& step, bool forward,
      const std::unordered_set<uint64_t>& breakpoints, const std::optional<breakpoint_skip>& skip, std::string& error
  ) const;

private:
  static bool is_skipped(const std::optional<breakpoint_skip>& skip, uint64_t sequence, uint64_t address);

  w1::rewind::block_decoder* decoder_ = nullptr;
};

} // namespace binja::rewind::core::engine
