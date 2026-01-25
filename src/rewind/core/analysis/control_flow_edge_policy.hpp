#pragma once

namespace binja::rewind::core::analysis {

struct branch_summary {
  bool is_call = false;
  bool is_branch = false;
  bool is_direct = false;
  bool is_indirect = false;
  bool is_return = false;
  bool is_syscall = false;
};

struct control_flow_edge_options {
  bool include_direct = true;
  bool include_indirect = true;
  bool include_calls = true;
  bool include_branches = true;
};

inline bool should_emit_edge(const branch_summary& summary, const control_flow_edge_options& options = {}) {
  if (summary.is_syscall) {
    return false;
  }
  if (summary.is_return) {
    return false;
  }
  if (!options.include_calls && summary.is_call) {
    return false;
  }
  if (!options.include_branches && summary.is_branch) {
    return false;
  }
  const bool allow_indirect = options.include_indirect && summary.is_indirect;
  const bool allow_direct = options.include_direct && summary.is_direct;
  return allow_indirect || allow_direct;
}

} // namespace binja::rewind::core::analysis
