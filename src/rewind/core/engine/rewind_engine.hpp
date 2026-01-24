#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "binaryninjaapi.h"
#include "rewind/core/breakpoints/breakpoint_provider.hpp"
#include "rewind/core/decode/bn_block_decoder.hpp"
#include "rewind/core/decode/instruction_decoder.hpp"
#include "rewind/core/functions/trace_function_definer.hpp"
#include "rewind/core/mapping/address_mapper.hpp"
#include "rewind/core/model/replay_types.hpp"
#include "rewind/core/update/update_builder.hpp"
#include "w1rewind/replay/flow_cursor.hpp"
#include "w1rewind/replay/replay_session.hpp"
#include "w1rewind/trace/trace_index.hpp"

namespace binja::rewind::core::engine {

class RewindEngine {
public:
  explicit RewindEngine(BinaryNinja::Ref<BinaryNinja::BinaryView> view);

  model::ReplayUpdate load_trace(const std::string& path);
  model::ReplayUpdate clear_trace();
  model::ReplayUpdate select_thread(uint64_t thread_id);

  model::ReplayUpdate step_instruction(bool forward);
  model::ReplayUpdate step_flow(bool forward);
  model::ReplayUpdate step_over();
  model::ReplayUpdate step_out();
  model::ReplayUpdate step_over_backward();
  model::ReplayUpdate step_out_backward();

  model::ReplayUpdate run_forward(const std::unordered_set<uint64_t>& breakpoints);
  model::ReplayUpdate run_backward(const std::unordered_set<uint64_t>& breakpoints);
  model::ReplayUpdate run_to_address(uint64_t trace_address, bool forward);
  model::ReplayUpdate run_to_view_address(uint64_t view_address, bool forward);
  model::ReplayUpdate run_to_start();

  model::ReplayUpdate define_functions_from_trace(const std::function<void(model::ReplayUpdate)>& progress = {});

  model::ReplayUpdate pause();
  void request_cancel();
  void set_gradient_size(size_t size);
  void set_reverse_history_size(size_t size);

  std::unordered_set<uint64_t> collect_breakpoints() const;

private:
  struct BreakpointResult {
    enum class Kind { none, exact, unresolved_block };
    Kind kind = Kind::none;
    uint64_t address = 0;
  };

  static constexpr size_t kStepGuardLimit = 1000000;
  static constexpr size_t kDefaultFastHistorySize = 1u << 16;

  static bool has_branch_type(const BinaryNinja::InstructionInfo& info, BNBranchType type);

  update::UpdateContext make_update_context();
  model::ReplayUpdate make_status_update(const std::string& status);
  model::ReplayUpdate make_error_update(const std::string& error);
  void attach_trace_info_if_dirty(model::ReplayUpdate& update);

  bool ensure_session_ready(std::string& error) const;
  bool ensure_position(std::string& error) const;
  bool ensure_instruction_position(bool forward, std::string& error);
  bool open_trace(const std::string& path, std::string& error, std::string& warning);
  void close_trace();
  bool move_to_sequence(uint64_t thread_id, uint64_t sequence, std::string& error);
  bool update_current_step(std::string& error);
  bool seek_to_address(uint64_t trace_address, bool forward, std::string& error, size_t max_steps = kStepGuardLimit);
  BreakpointResult find_breakpoint_in_current_block(
      const std::unordered_set<uint64_t>& breakpoints, bool forward, std::string& error
  );
  BreakpointResult find_breakpoint_hit(
      const w1::rewind::flow_step& step, const std::unordered_set<uint64_t>& breakpoints, bool forward,
      std::string& error
  );

  model::ReplayUpdate run_flow(bool forward, const std::unordered_set<uint64_t>& breakpoints);

  BinaryNinja::Ref<BinaryNinja::BinaryView> view_;
  BinaryNinja::Ref<BinaryNinja::Logger> logger_;

  mapping::AddressMapper mapper_{};
  decode::BnBlockDecoder block_decoder_{};
  decode::InstructionDecoder instruction_decoder_{};
  breakpoints::BreakpointProvider breakpoint_provider_{};
  update::UpdateBuilder update_builder_{};
  functions::TraceFunctionDefiner function_definer_{};

  std::string trace_path_;
  std::string trace_index_path_;
  bool trace_loaded_ = false;
  bool controls_enabled_ = false;
  std::vector<model::ThreadInfo> threads_;
  model::TraceSummary trace_summary_{};
  std::vector<model::TraceModule> trace_modules_;
  bool trace_info_dirty_ = false;
  uint64_t current_thread_ = 0;
  bool has_position_ = false;
  w1::rewind::flow_step current_step_{};
  size_t gradient_size_ = 8;
  size_t fast_history_size_ = kDefaultFastHistorySize;

  std::shared_ptr<w1::rewind::trace_index> trace_index_;
  std::optional<w1::rewind::replay_session> session_;
  std::optional<w1::rewind::flow_cursor> fast_cursor_;

  std::atomic<bool> run_active_{false};
  std::atomic<uint64_t> cancel_epoch_{0};
};

} // namespace binja::rewind::core::engine
