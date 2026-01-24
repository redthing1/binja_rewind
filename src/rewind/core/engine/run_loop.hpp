#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

#include "rewind/core/engine/breakpoint_matcher.hpp"
#include "w1rewind/replay/flow_cursor.hpp"
#include "w1rewind/replay/replay_context.hpp"

namespace binja::rewind::core::engine {

enum class run_stop_reason { hit_exact, hit_in_block, end_of_trace, begin_of_trace, cancelled, error };

struct run_stop {
  run_stop_reason reason = run_stop_reason::error;
  std::optional<uint64_t> hit_address;
  std::optional<w1::rewind::flow_step> last_step;
  std::string detail;
};

class run_loop {
public:
  run_loop(
      w1::rewind::flow_cursor* cursor, breakpoint_matcher* matcher, const w1::rewind::replay_context* context
  )
      : cursor_(cursor), matcher_(matcher), context_(context) {}

  run_stop run(
      bool forward, const std::unordered_set<uint64_t>& breakpoints, const std::optional<breakpoint_skip>& skip,
      const std::function<bool()>& cancelled
  ) const;

private:
  w1::rewind::flow_cursor* cursor_ = nullptr;
  breakpoint_matcher* matcher_ = nullptr;
  const w1::rewind::replay_context* context_ = nullptr;
};

} // namespace binja::rewind::core::engine
