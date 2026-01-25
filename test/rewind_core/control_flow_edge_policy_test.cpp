#include "doctest/doctest.hpp"

#include "rewind/core/analysis/control_flow_edge_policy.hpp"

TEST_CASE("control flow edge policy emits call edges") {
  binja::rewind::core::analysis::branch_summary summary{};
  summary.is_call = true;
  summary.is_direct = true;
  CHECK(binja::rewind::core::analysis::should_emit_edge(summary));
}

TEST_CASE("control flow edge policy emits branch edges") {
  binja::rewind::core::analysis::branch_summary summary{};
  summary.is_branch = true;
  summary.is_indirect = true;
  CHECK(binja::rewind::core::analysis::should_emit_edge(summary));
}

TEST_CASE("control flow edge policy can exclude direct edges") {
  binja::rewind::core::analysis::branch_summary summary{};
  summary.is_call = true;
  summary.is_direct = true;
  binja::rewind::core::analysis::control_flow_edge_options options{};
  options.include_direct = false;
  CHECK_FALSE(binja::rewind::core::analysis::should_emit_edge(summary, options));
}

TEST_CASE("control flow edge policy can exclude calls") {
  binja::rewind::core::analysis::branch_summary summary{};
  summary.is_call = true;
  summary.is_indirect = true;
  binja::rewind::core::analysis::control_flow_edge_options options{};
  options.include_calls = false;
  CHECK_FALSE(binja::rewind::core::analysis::should_emit_edge(summary, options));
}

TEST_CASE("control flow edge policy ignores returns") {
  binja::rewind::core::analysis::branch_summary summary{};
  summary.is_call = true;
  summary.is_direct = true;
  summary.is_return = true;
  CHECK_FALSE(binja::rewind::core::analysis::should_emit_edge(summary));
}

TEST_CASE("control flow edge policy ignores syscalls") {
  binja::rewind::core::analysis::branch_summary summary{};
  summary.is_branch = true;
  summary.is_indirect = true;
  summary.is_syscall = true;
  CHECK_FALSE(binja::rewind::core::analysis::should_emit_edge(summary));
}
