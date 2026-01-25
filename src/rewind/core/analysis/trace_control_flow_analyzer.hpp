#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "binaryninjaapi.h"
#include "rewind/core/analysis/control_flow_edge_policy.hpp"
#include "rewind/core/decode/instruction_decoder.hpp"
#include "rewind/core/mapping/address_mapper.hpp"
#include "w1rewind/replay/replay_session.hpp"
#include "w1rewind/trace/trace_index.hpp"

namespace binja::rewind::core::analysis {

struct ControlFlowEdgeResult {
  bool ok = false;
  std::string error;
  size_t steps_scanned = 0;
  size_t transitions = 0;
  size_t edges_found = 0;
  size_t edges_added = 0;
  size_t edges_skipped_no_branch = 0;
  size_t edges_skipped_no_mapping = 0;
  size_t edges_skipped_decode = 0;
  size_t edges_skipped_no_segment = 0;
  size_t edges_skipped_no_function = 0;
  size_t edges_skipped_no_arch = 0;
  size_t edges_skipped_existing = 0;
  size_t edges_skipped_return_gap = 0;
  size_t blocks_decoded = 0;
};

class TraceControlFlowAnalyzer {
public:
  ControlFlowEdgeResult add_control_flow_edges(
      const w1::rewind::replay_session& session, const std::shared_ptr<w1::rewind::trace_index>& index,
      const mapping::AddressMapper& mapper, const BinaryNinja::Ref<BinaryNinja::BinaryView>& view,
      const std::string& trace_path, const BinaryNinja::Ref<BinaryNinja::Logger>& logger,
      const std::function<void(const std::string&)>& progress = {}, const std::function<bool()>& is_cancelled = {},
      const control_flow_edge_options& options = {}
  ) const;
};

} // namespace binja::rewind::core::analysis
