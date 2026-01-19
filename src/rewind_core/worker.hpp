#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "binaryninjaapi.h"
#include "rewind_core/address_mapper.hpp"
#include "rewind_core/bn_block_decoder.hpp"
#include "rewind_core/breakpoint_provider.hpp"
#include "w1rewind/replay/replay_flow_cursor.hpp"
#include "w1rewind/replay/replay_instruction_cursor.hpp"
#include "w1rewind/replay/replay_session.hpp"

namespace binja_rewind {

struct ThreadInfo {
  uint64_t id = 0;
  std::string name;
  bool started = false;
  bool ended = false;
};

struct RegisterValue {
  std::string name;
  std::string value;
  bool known = false;
};

struct TraceSummary {
  std::string arch;
  std::string os;
  std::string abi;
  std::string cpu;
  uint16_t trace_version = 0;
  bool has_blocks = false;
  bool has_registers = false;
  bool has_memory_access = false;
  bool has_memory_values = false;
  bool has_stack_snapshot = false;
  uint64_t thread_count = 0;
  uint64_t module_count = 0;
};

struct TraceModule {
  std::string path;
  uint64_t base = 0;
  uint64_t size = 0;
  uint32_t permissions = 0;
};

struct ReplayUpdate {
  std::string status;
  std::string trace_path;
  bool trace_loaded = false;
  bool trace_cleared = false;
  bool controls_enabled = false;

  bool has_position = false;
  uint64_t thread_id = 0;
  uint64_t sequence = 0;
  uint64_t trace_address = 0;
  std::optional<uint64_t> view_address;

  std::vector<ThreadInfo> threads;
  std::vector<RegisterValue> registers;
  std::vector<uint64_t> past_addresses;
  std::vector<uint64_t> future_addresses;

  bool trace_info_changed = false;
  TraceSummary summary;
  std::vector<TraceModule> modules;
};

class RewindWorker {
public:
  using UpdateCallback = std::function<void(const ReplayUpdate&)>;

  explicit RewindWorker(BinaryNinja::Ref<BinaryNinja::BinaryView> view, UpdateCallback callback);
  ~RewindWorker();

  void load_trace(const std::string& path);
  void clear_trace();
  void select_thread(uint64_t thread_id);

  void step_instruction();
  void step_instruction_backward();
  void step_flow();
  void step_flow_backward();
  void step_over();
  void step_out();

  void run_forward();
  void run_backward();
  void run_to_start();
  void pause();
  void set_gradient_size(size_t size);

private:
  static constexpr size_t kStepGuardLimit = 1000000;

  struct GradientSample {
    uint64_t current = 0;
    std::vector<uint64_t> past;
    std::vector<uint64_t> future;
  };

  void enqueue(std::function<void()> task);
  void worker_loop();
  void post_update(ReplayUpdate update);
  void post_status(const std::string& status, bool controls_enabled);
  void post_update_status(const std::string& status);
  void post_error(const std::string& error);

  bool open_trace(const std::string& path, std::string& error, std::string& warning);
  void close_trace();
  bool ensure_session_ready(std::string& error);
  bool ensure_position(std::string& error) const;
  bool ensure_instruction_position(std::string& error);
  bool move_to_sequence(uint64_t thread_id, uint64_t sequence, std::string& error);
  bool update_current_step(std::string& error);
  bool seek_to_address(uint64_t trace_address, bool forward, std::string& error, size_t max_steps = kStepGuardLimit);
  bool decode_instruction(
      uint64_t trace_address, BinaryNinja::InstructionInfo& info, size_t& length, std::string& error
  );
  static bool has_branch_type(const BinaryNinja::InstructionInfo& info, BNBranchType type);
  std::optional<uint64_t> find_breakpoint_in_current_block(
      const std::unordered_set<uint64_t>& breakpoints, bool forward, std::string& error
  );
  void step_instruction_impl(bool forward);
  void step_flow_impl(bool forward);
  void run_flow_impl(bool forward);
  void fill_update(ReplayUpdate& update);
  void fill_registers(ReplayUpdate& update);
  bool sample_gradient(GradientSample& sample);
  std::optional<uint64_t> find_breakpoint_hit(
      const w1::rewind::flow_step& step, const std::unordered_set<uint64_t>& breakpoints, bool forward,
      std::string& error
  );

  BinaryNinja::Ref<BinaryNinja::BinaryView> view_;
  BinaryNinja::Ref<BinaryNinja::Logger> logger_;
  UpdateCallback callback_;
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> queue_;
  bool stop_ = false;

  std::atomic<bool> run_active_{false};
  std::atomic<bool> cancel_requested_{false};

  std::string trace_path_;
  bool trace_loaded_ = false;
  bool controls_enabled_ = false;
  std::vector<ThreadInfo> threads_;
  TraceSummary trace_summary_{};
  std::vector<TraceModule> trace_modules_{};
  bool trace_info_dirty_ = false;
  uint64_t current_thread_ = 0;
  bool has_position_ = false;
  w1::rewind::flow_step current_step_{};
  size_t gradient_size_ = 8;

  AddressMapper mapper_{};
  BnBlockDecoder block_decoder_{};
  BreakpointProvider breakpoint_provider_{};

  std::optional<w1::rewind::replay_session> session_;
  std::optional<w1::rewind::replay_flow_cursor> fast_cursor_;
};

} // namespace binja_rewind
