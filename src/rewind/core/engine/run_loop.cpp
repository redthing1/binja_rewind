#include "rewind/core/engine/run_loop.hpp"

namespace binja::rewind::core::engine {

run_stop run_loop::run(
    bool forward, const std::unordered_set<uint64_t>& breakpoints, const std::optional<breakpoint_skip>& skip,
    const std::function<bool()>& cancelled
) const {
  run_stop stop{};
  if (!cursor_ || !matcher_ || !context_) {
    stop.reason = run_stop_reason::error;
    stop.detail = "run loop missing dependencies";
    return stop;
  }

  w1::rewind::flow_step step{};
  for (;;) {
    if (cancelled && cancelled()) {
      stop.reason = run_stop_reason::cancelled;
      return stop;
    }

    bool ok = forward ? cursor_->step_forward(step) : cursor_->step_backward(step);
    if (!ok) {
      if (cancelled && cancelled()) {
        stop.reason = run_stop_reason::cancelled;
        return stop;
      }
      auto kind = cursor_->error_kind();
      if (forward && kind == w1::rewind::flow_error_kind::end_of_trace) {
        stop.reason = run_stop_reason::end_of_trace;
      } else if (!forward && kind == w1::rewind::flow_error_kind::begin_of_trace) {
        stop.reason = run_stop_reason::begin_of_trace;
      } else {
        stop.reason = run_stop_reason::error;
        stop.detail = std::string(cursor_->error());
      }
      return stop;
    }

    stop.last_step = step;

    std::string error;
    auto hit = matcher_->match_step(*context_, step, forward, breakpoints, skip, error);
    if (hit.kind == breakpoint_match_kind::exact) {
      stop.reason = run_stop_reason::hit_exact;
      stop.hit_address = hit.address;
      return stop;
    }
    if (hit.kind == breakpoint_match_kind::unresolved_block) {
      stop.reason = run_stop_reason::hit_in_block;
      stop.hit_address = hit.address;
      stop.detail = error;
      return stop;
    }
  }
}

} // namespace binja::rewind::core::engine
