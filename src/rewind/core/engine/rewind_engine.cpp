#include "rewind/core/engine/rewind_engine.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

#include "w1base/arch_spec.hpp"
#include "w1rewind/trace/trace_reader.hpp"

namespace binja::rewind::core::engine {

bool RewindEngine::has_branch_type(const BinaryNinja::InstructionInfo& info, BNBranchType type) {
  for (size_t i = 0; i < info.branchCount; ++i) {
    if (info.branchType[i] == type) {
      return true;
    }
  }
  return false;
}

RewindEngine::RewindEngine(BinaryNinja::Ref<BinaryNinja::BinaryView> view) : view_(std::move(view)) {
  if (view_) {
    logger_ = view_->CreateLogger("Rewind");
  } else {
    logger_ = BinaryNinja::LogRegistry::CreateLogger("Rewind");
  }
  mapper_.set_logger(logger_);
  block_decoder_.set_view(view_);
  instruction_decoder_.set_view(view_);
}

update::UpdateContext RewindEngine::make_update_context() {
  update::UpdateContext ctx{};
  ctx.session = session_ ? &(*session_) : nullptr;
  ctx.mapper = &mapper_;
  ctx.block_decoder = &block_decoder_;
  ctx.trace_path = &trace_path_;
  ctx.trace_index = trace_index_;
  ctx.trace_loaded = trace_loaded_;
  ctx.controls_enabled = controls_enabled_;
  ctx.has_position = has_position_;
  ctx.current_thread = current_thread_;
  ctx.current_step = &current_step_;
  ctx.gradient_size = gradient_size_;
  return ctx;
}

model::ReplayUpdate RewindEngine::make_status_update(const std::string& status) {
  model::ReplayUpdate update{};
  update.status = status;
  update_builder_.fill_update(make_update_context(), update);
  return update;
}

model::ReplayUpdate RewindEngine::make_error_update(const std::string& error) {
  return make_status_update("Error: " + error);
}

void RewindEngine::attach_trace_info_if_dirty(model::ReplayUpdate& update) {
  if (!trace_info_dirty_) {
    return;
  }
  update.trace_info_changed = true;
  update.summary = trace_summary_;
  update.modules = trace_modules_;
  trace_info_dirty_ = false;
}

bool RewindEngine::ensure_session_ready(std::string& error) const {
  if (!trace_loaded_ || !session_.has_value()) {
    error = "trace not loaded";
    return false;
  }
  return true;
}

bool RewindEngine::ensure_position(std::string& error) const {
  if (!has_position_) {
    error = "No current position";
    return false;
  }
  return true;
}

bool RewindEngine::ensure_instruction_position(std::string& error) {
  if (!current_step_.is_block) {
    return true;
  }
  if (!session_.has_value()) {
    error = "session not ready";
    return false;
  }
  if (!session_->sync_instruction_position()) {
    error = session_->error();
    return false;
  }
  return update_current_step(error);
}

bool RewindEngine::open_trace(const std::string& path, std::string& error, std::string& warning) {
  close_trace();
  warning.clear();

  trace_path_ = path;
  trace_index_.reset();
  trace_index_path_.clear();
  w1::rewind::replay_context context;
  if (!w1::rewind::load_replay_context(path, context, error)) {
    return false;
  }

  if (logger_) {
    logger_->LogInfo("Rewind: loading trace '%s'", path.c_str());
  }

  const bool has_register_names = !context.register_names.empty();
  const bool has_register_specs = !context.register_specs.empty();
  const bool has_registers = has_register_names && has_register_specs;
  auto features = context.features();

  trace_summary_ = model::TraceSummary{};
  trace_summary_.trace_version = context.header.version;
  trace_summary_.arch = std::string(w1::arch::gdb_arch_name(context.header.arch));
  if (context.target_info.has_value()) {
    trace_summary_.os = context.target_info->os;
    trace_summary_.abi = context.target_info->abi;
    trace_summary_.cpu = context.target_info->cpu;
  }
  trace_summary_.has_blocks = features.has_blocks;
  trace_summary_.has_registers = features.has_registers;
  trace_summary_.has_memory_access = features.has_memory_access;
  trace_summary_.has_memory_values = features.has_memory_values;
  trace_summary_.has_stack_snapshot = features.has_stack_snapshot;
  trace_summary_.thread_count = context.threads.size();
  trace_summary_.module_count = context.modules.size();

  trace_modules_.clear();
  trace_modules_.reserve(context.modules.size());
  for (const auto& module : context.modules) {
    model::TraceModule info{};
    info.path = module.path;
    info.base = module.base;
    info.size = module.size;
    info.permissions = static_cast<uint32_t>(module.permissions);
    trace_modules_.push_back(std::move(info));
  }
  trace_info_dirty_ = true;

  w1::rewind::trace_index index;
  w1::rewind::trace_index_options index_options;
  std::string index_error;
  std::filesystem::path trace_file(path);
  std::filesystem::path index_path = w1::rewind::default_trace_index_path(path);
  if (!w1::rewind::ensure_trace_index(trace_file, index_path, index_options, index, index_error, true)) {
    error = index_error.empty() ? "failed to load trace index" : index_error;
    return false;
  }
  trace_index_ = std::make_shared<w1::rewind::trace_index>(std::move(index));
  trace_index_path_ = index_path.string();

  auto session_stream = std::make_shared<w1::rewind::trace_reader>(path);

  w1::rewind::replay_session_config config{};
  config.stream = session_stream;
  config.index = trace_index_;
  config.context = std::move(context);
  config.history_size = 4096;
  config.track_registers = has_registers;
  config.track_memory = has_registers && features.track_memory;
  config.block_decoder = &block_decoder_;

  session_.emplace(config);
  if (!session_->open()) {
    error = session_->error();
    session_.reset();
    return false;
  }

  if (!mapper_.configure(view_, session_->context(), &error)) {
    session_.reset();
    return false;
  }
  block_decoder_.set_mapper(&mapper_);
  instruction_decoder_.set_mapper(&mapper_);

  threads_.clear();
  for (const auto& thread : session_->threads()) {
    model::ThreadInfo info{};
    info.id = thread.thread_id;
    info.name = thread.name;
    info.started = thread.started;
    info.ended = thread.ended;
    threads_.push_back(std::move(info));
  }

  current_thread_ = threads_.empty() ? 0 : threads_.front().id;
  has_position_ = false;
  current_step_ = w1::rewind::flow_step{};

  if (current_thread_ != 0) {
    if (!session_->select_thread(current_thread_, 0)) {
      warning = session_->error();
    } else if (session_->step_flow()) {
      current_step_ = session_->current_step();
      has_position_ = true;
    } else {
      warning = session_->error();
    }
  }

  auto fast_stream = std::make_shared<w1::rewind::trace_reader>(path);
  w1::rewind::flow_cursor_config cursor_config{};
  cursor_config.stream = fast_stream;
  cursor_config.index = trace_index_;
  cursor_config.history_size = 4096;
  cursor_config.context = &session_->context();

  fast_cursor_.emplace(cursor_config);
  if (!fast_cursor_->open()) {
    error = std::string(fast_cursor_->error());
    fast_cursor_.reset();
    return false;
  }

  trace_loaded_ = true;
  controls_enabled_ = true;
  return true;
}

void RewindEngine::close_trace() {
  session_.reset();
  fast_cursor_.reset();
  trace_index_.reset();
  trace_index_path_.clear();
  threads_.clear();
  trace_summary_ = model::TraceSummary{};
  trace_modules_.clear();
  trace_info_dirty_ = true;
  current_thread_ = 0;
  has_position_ = false;
  current_step_ = w1::rewind::flow_step{};
  trace_loaded_ = false;
  controls_enabled_ = false;
}

bool RewindEngine::move_to_sequence(uint64_t thread_id, uint64_t sequence, std::string& error) {
  if (!session_.has_value()) {
    error = "session not ready";
    return false;
  }
  if (!session_->select_thread(thread_id, sequence)) {
    error = session_->error();
    return false;
  }
  if (!session_->step_flow()) {
    error = session_->error();
    return false;
  }
  current_step_ = session_->current_step();
  has_position_ = true;
  current_thread_ = thread_id;
  return true;
}

bool RewindEngine::update_current_step(std::string& error) {
  if (!session_.has_value()) {
    error = "session not ready";
    return false;
  }
  current_step_ = session_->current_step();
  has_position_ = true;
  current_thread_ = current_step_.thread_id;
  return true;
}

bool RewindEngine::seek_to_address(uint64_t trace_address, bool forward, std::string& error, size_t max_steps) {
  if (!session_.has_value()) {
    error = "session not ready";
    return false;
  }
  if (!has_position_) {
    error = "no current position";
    return false;
  }

  if (current_step_.address == trace_address) {
    return true;
  }

  size_t steps = 0;
  while (steps < max_steps) {
    bool ok = forward ? session_->step_instruction() : session_->step_instruction_backward();
    if (!ok) {
      error = session_->error();
      return false;
    }
    if (!update_current_step(error)) {
      return false;
    }
    if (current_step_.address == trace_address) {
      return true;
    }
    ++steps;
  }

  error = "address not reached within step limit";
  return false;
}

model::ReplayUpdate RewindEngine::load_trace(const std::string& path) {
  cancel_requested_.store(true);
  run_active_.store(false);

  std::string error;
  std::string warning;
  if (!open_trace(path, error, warning)) {
    if (logger_) {
      logger_->LogError("Rewind: failed to load trace '%s': %s", path.c_str(), error.c_str());
    }
    model::ReplayUpdate update{};
    update.status = "Error: " + error;
    update.trace_cleared = true;
    update.controls_enabled = false;
    attach_trace_info_if_dirty(update);
    return update;
  }

  model::ReplayUpdate update{};
  update.trace_loaded = true;
  update.controls_enabled = true;
  update.status = "Trace loaded";
  update.trace_path = trace_path_;
  update.threads = threads_;
  update_builder_.fill_update(make_update_context(), update);
  attach_trace_info_if_dirty(update);
  return update;
}

model::ReplayUpdate RewindEngine::clear_trace() {
  cancel_requested_.store(true);
  run_active_.store(false);
  close_trace();
  model::ReplayUpdate update{};
  update.trace_cleared = true;
  update.controls_enabled = false;
  update.status = "Trace cleared";
  attach_trace_info_if_dirty(update);
  return update;
}

model::ReplayUpdate RewindEngine::select_thread(uint64_t thread_id) {
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!move_to_sequence(thread_id, 0, error)) {
    return make_error_update(error);
  }
  model::ReplayUpdate update{};
  update.status = "Thread selected";
  update_builder_.fill_update(make_update_context(), update);
  return update;
}

model::ReplayUpdate RewindEngine::step_instruction(bool forward) {
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!ensure_instruction_position(error)) {
    return make_error_update(error);
  }

  bool ok = forward ? session_->step_instruction() : session_->step_instruction_backward();
  if (!ok) {
    return make_error_update(session_->error());
  }

  std::string notice;
  if (auto session_notice = session_->take_notice(); session_notice.has_value()) {
    notice = session_notice->message;
  }

  if (!update_current_step(error)) {
    return make_error_update(error);
  }

  model::ReplayUpdate update{};
  if (!notice.empty()) {
    update.status = notice;
  } else {
    update.status = forward ? "Stepped" : "Stepped back";
  }
  update_builder_.fill_update(make_update_context(), update);
  return update;
}

model::ReplayUpdate RewindEngine::step_flow(bool forward) {
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }

  bool ok = forward ? session_->step_flow() : session_->step_backward();
  if (!ok) {
    return make_error_update(session_->error());
  }
  if (!update_current_step(error)) {
    return make_error_update(error);
  }

  model::ReplayUpdate update{};
  update.status = forward ? "Stepped" : "Stepped back";
  update_builder_.fill_update(make_update_context(), update);
  return update;
}

model::ReplayUpdate RewindEngine::step_over() {
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!ensure_position(error)) {
    return make_status_update(error);
  }
  if (!ensure_instruction_position(error)) {
    return make_error_update(error);
  }

  auto step_forward = [&](std::string& step_error) -> bool {
    if (!session_->step_instruction()) {
      step_error = session_->error();
      return false;
    }
    return update_current_step(step_error);
  };

  BinaryNinja::InstructionInfo info{};
  size_t length = 0;
  if (!instruction_decoder_.decode_instruction(current_step_.address, info, length, error)) {
    if (!step_forward(error)) {
      return make_error_update(error);
    }
    return make_status_update("Stepped");
  }

  bool is_call = has_branch_type(info, CallDestination) || has_branch_type(info, SystemCall);
  if (!is_call) {
    if (!step_forward(error)) {
      return make_error_update(error);
    }
    return make_status_update("Stepped");
  }

  uint64_t target_address = current_step_.address + length;
  if (!seek_to_address(target_address, true, error)) {
    return make_error_update(error);
  }

  return make_status_update("Step over");
}

model::ReplayUpdate RewindEngine::step_out() {
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!ensure_position(error)) {
    return make_status_update(error);
  }
  if (!ensure_instruction_position(error)) {
    return make_error_update(error);
  }

  auto step_forward = [&](std::string& step_error) -> bool {
    if (!session_->step_instruction()) {
      step_error = session_->error();
      return false;
    }
    return update_current_step(step_error);
  };

  BinaryNinja::InstructionInfo info{};
  size_t length = 0;
  if (instruction_decoder_.decode_instruction(current_step_.address, info, length, error) &&
      has_branch_type(info, FunctionReturn)) {
    if (!step_forward(error)) {
      return make_error_update(error);
    }
    return make_status_update("Step out");
  }

  int depth = 0;
  size_t guard = 0;
  for (;;) {
    if (!step_forward(error)) {
      return make_error_update(error);
    }
    if (++guard > kStepGuardLimit) {
      return make_error_update("step out exceeded step limit");
    }

    BinaryNinja::InstructionInfo step_info{};
    size_t step_len = 0;
    std::string decode_error;
    if (instruction_decoder_.decode_instruction(current_step_.address, step_info, step_len, decode_error)) {
      if (has_branch_type(step_info, CallDestination) || has_branch_type(step_info, SystemCall)) {
        depth++;
      }
      if (has_branch_type(step_info, FunctionReturn)) {
        if (depth == 0) {
          if (!step_forward(error)) {
            return make_error_update(error);
          }
          break;
        }
        depth = std::max(0, depth - 1);
      }
    }
  }

  return make_status_update("Step out");
}

model::ReplayUpdate RewindEngine::step_over_backward() {
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!ensure_position(error)) {
    return make_status_update(error);
  }
  if (!ensure_instruction_position(error)) {
    return make_error_update(error);
  }

  auto step_backward = [&](std::string& step_error) -> bool {
    if (!session_->step_instruction_backward()) {
      step_error = session_->error();
      return false;
    }
    return update_current_step(step_error);
  };

  if (!step_backward(error)) {
    return make_error_update(error);
  }

  BinaryNinja::InstructionInfo info{};
  size_t length = 0;
  if (!instruction_decoder_.decode_instruction(current_step_.address, info, length, error)) {
    return make_status_update("Stepped back");
  }

  bool is_call = has_branch_type(info, CallDestination) || has_branch_type(info, SystemCall);
  bool is_return = has_branch_type(info, FunctionReturn);

  if (is_call && !is_return) {
    return make_status_update("Step over back");
  }

  if (!is_return) {
    return make_status_update("Stepped back");
  }

  int depth = 1;
  size_t guard = 0;
  while (depth > 0) {
    if (!step_backward(error)) {
      return make_error_update(error);
    }
    if (++guard > kStepGuardLimit) {
      return make_error_update("step over back exceeded step limit");
    }

    BinaryNinja::InstructionInfo step_info{};
    size_t step_len = 0;
    std::string decode_error;
    if (instruction_decoder_.decode_instruction(current_step_.address, step_info, step_len, decode_error)) {
      if (has_branch_type(step_info, FunctionReturn)) {
        depth++;
      }
      if (has_branch_type(step_info, CallDestination) || has_branch_type(step_info, SystemCall)) {
        depth = std::max(0, depth - 1);
        if (depth == 0) {
          break;
        }
      }
    }
  }

  return make_status_update("Step over back");
}

model::ReplayUpdate RewindEngine::step_out_backward() {
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!ensure_position(error)) {
    return make_status_update(error);
  }
  if (!ensure_instruction_position(error)) {
    return make_error_update(error);
  }

  auto step_backward = [&](std::string& step_error) -> bool {
    if (!session_->step_instruction_backward()) {
      step_error = session_->error();
      return false;
    }
    return update_current_step(step_error);
  };

  int depth = 0;
  size_t guard = 0;
  for (;;) {
    if (!step_backward(error)) {
      return make_error_update(error);
    }
    if (++guard > kStepGuardLimit) {
      return make_error_update("step out back exceeded step limit");
    }

    BinaryNinja::InstructionInfo step_info{};
    size_t step_len = 0;
    std::string decode_error;
    if (instruction_decoder_.decode_instruction(current_step_.address, step_info, step_len, decode_error)) {
      if (has_branch_type(step_info, FunctionReturn)) {
        depth++;
      }
      if (has_branch_type(step_info, CallDestination) || has_branch_type(step_info, SystemCall)) {
        if (depth == 0) {
          break;
        }
        depth = std::max(0, depth - 1);
      }
    }
  }

  return make_status_update("Step out back");
}

model::ReplayUpdate RewindEngine::run_to_start() {
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!move_to_sequence(current_thread_, 0, error)) {
    return make_error_update(error);
  }
  return make_status_update("Rewound to start");
}

model::ReplayUpdate RewindEngine::pause() {
  cancel_requested_.store(true);
  return make_status_update("Paused");
}

void RewindEngine::request_cancel() { cancel_requested_.store(true); }

void RewindEngine::set_gradient_size(size_t size) {
  size_t clamped = size;
  if (clamped < 1) {
    clamped = 1;
  } else if (clamped > 64) {
    clamped = 64;
  }
  gradient_size_ = clamped;
}

std::unordered_set<uint64_t> RewindEngine::collect_breakpoints() const {
  return breakpoint_provider_.collect_breakpoints(view_, mapper_);
}

std::optional<uint64_t> RewindEngine::find_breakpoint_in_current_block(
    const std::unordered_set<uint64_t>& breakpoints, bool forward, std::string& error
) {
  error.clear();
  if (breakpoints.empty() || !session_.has_value() || !has_position_) {
    return std::nullopt;
  }
  const auto& context = session_->context();
  if (!context.has_blocks()) {
    return std::nullopt;
  }
  if (current_step_.is_block || current_step_.block_id == 0) {
    return std::nullopt;
  }

  auto it = context.blocks_by_id.find(current_step_.block_id);
  if (it == context.blocks_by_id.end()) {
    return std::nullopt;
  }

  w1::rewind::flow_step block_step = current_step_;
  block_step.is_block = true;
  block_step.address = it->second.address;
  block_step.size = it->second.size;

  if (block_step.size == 0) {
    return std::nullopt;
  }

  bool candidate = false;
  uint64_t block_start = block_step.address;
  uint64_t block_end = block_start + block_step.size;
  for (const auto& bp : breakpoints) {
    if (bp >= block_start && bp < block_end) {
      candidate = true;
      break;
    }
  }
  if (!candidate) {
    return std::nullopt;
  }

  w1::rewind::replay_decoded_block decoded{};
  if (!block_decoder_.decode_block(context, block_step, decoded, error)) {
    return std::nullopt;
  }

  size_t current_index = decoded.instructions.size();
  for (size_t i = 0; i < decoded.instructions.size(); ++i) {
    uint64_t addr = decoded.address + decoded.instructions[i].offset;
    if (addr == current_step_.address) {
      current_index = i;
      break;
    }
  }
  if (current_index == decoded.instructions.size()) {
    return std::nullopt;
  }

  if (forward) {
    for (size_t i = current_index + 1; i < decoded.instructions.size(); ++i) {
      uint64_t addr = decoded.address + decoded.instructions[i].offset;
      if (breakpoints.find(addr) != breakpoints.end()) {
        return addr;
      }
    }
  } else {
    for (size_t i = current_index; i-- > 0;) {
      uint64_t addr = decoded.address + decoded.instructions[i].offset;
      if (breakpoints.find(addr) != breakpoints.end()) {
        return addr;
      }
    }
  }

  return std::nullopt;
}

std::optional<uint64_t> RewindEngine::find_breakpoint_hit(
    const w1::rewind::flow_step& step, const std::unordered_set<uint64_t>& breakpoints, bool forward, std::string& error
) {
  error.clear();
  if (breakpoints.empty()) {
    return std::nullopt;
  }
  if (!session_.has_value()) {
    return std::nullopt;
  }
  const auto& context = session_->context();
  if (!context.has_blocks()) {
    if (breakpoints.find(step.address) != breakpoints.end()) {
      return step.address;
    }
    return std::nullopt;
  }

  if (breakpoints.find(step.address) != breakpoints.end()) {
    return step.address;
  }

  if (step.size == 0) {
    return std::nullopt;
  }

  bool candidate = false;
  uint64_t block_start = step.address;
  uint64_t block_end = block_start + step.size;
  for (const auto& bp : breakpoints) {
    if (bp >= block_start && bp < block_end) {
      candidate = true;
      break;
    }
  }
  if (!candidate) {
    return std::nullopt;
  }

  w1::rewind::replay_decoded_block decoded{};
  if (!block_decoder_.decode_block(context, step, decoded, error)) {
    return step.address;
  }

  if (forward) {
    for (const auto& inst : decoded.instructions) {
      uint64_t addr = decoded.address + inst.offset;
      if (breakpoints.find(addr) != breakpoints.end()) {
        return addr;
      }
    }
  } else {
    for (auto it = decoded.instructions.rbegin(); it != decoded.instructions.rend(); ++it) {
      uint64_t addr = decoded.address + it->offset;
      if (breakpoints.find(addr) != breakpoints.end()) {
        return addr;
      }
    }
  }

  return step.address;
}

model::ReplayUpdate RewindEngine::run_flow(bool forward, const std::unordered_set<uint64_t>& breakpoints) {
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!ensure_position(error)) {
    return make_status_update(error);
  }
  if (!fast_cursor_) {
    return make_error_update("Flow cursor unavailable");
  }

  if (!current_step_.is_block) {
    std::string bp_error;
    auto immediate = find_breakpoint_in_current_block(breakpoints, forward, bp_error);
    if (immediate.has_value()) {
      if (!seek_to_address(*immediate, forward, error)) {
        return make_error_update(error);
      }
      model::ReplayUpdate update{};
      update.status = "Breakpoint hit";
      update_builder_.fill_update(make_update_context(), update);
      return update;
    }
  }

  if (!fast_cursor_->seek(current_thread_, current_step_.sequence)) {
    return make_error_update(std::string(fast_cursor_->error()));
  }

  w1::rewind::flow_step step{};
  if (!fast_cursor_->step_forward(step)) {
    auto kind = fast_cursor_->error_kind();
    if (kind == w1::rewind::flow_error_kind::end_of_trace) {
      return make_status_update("End of trace");
    }
    return make_error_update(std::string(fast_cursor_->error()));
  }

  cancel_requested_.store(false);
  run_active_.store(true);

  std::optional<w1::rewind::flow_step> last_step;
  std::optional<uint64_t> hit_address;
  std::string stop_reason;
  for (;;) {
    if (cancel_requested_.load()) {
      stop_reason = "Paused";
      break;
    }

    bool ok = forward ? fast_cursor_->step_forward(step) : fast_cursor_->step_backward(step);
    if (!ok) {
      auto kind = fast_cursor_->error_kind();
      if (forward && kind == w1::rewind::flow_error_kind::end_of_trace) {
        stop_reason = "End of trace";
      } else if (!forward && kind == w1::rewind::flow_error_kind::begin_of_trace) {
        stop_reason = "Start of trace";
      } else {
        stop_reason = std::string(fast_cursor_->error());
      }
      break;
    }

    last_step = step;
    std::string bp_error;
    auto hit = find_breakpoint_hit(step, breakpoints, forward, bp_error);
    if (hit.has_value()) {
      stop_reason = "Breakpoint hit";
      hit_address = hit;
      break;
    }
  }

  run_active_.store(false);
  cancel_requested_.store(false);

  if (last_step.has_value()) {
    if (!move_to_sequence(current_thread_, last_step->sequence, error)) {
      return make_error_update(error);
    }
    if (hit_address.has_value() && current_step_.address != *hit_address) {
      if (!seek_to_address(*hit_address, true, error)) {
        return make_error_update(error);
      }
    }
  }

  model::ReplayUpdate update{};
  update.status = stop_reason.empty() ? "Stopped" : stop_reason;
  update_builder_.fill_update(make_update_context(), update);
  return update;
}

model::ReplayUpdate RewindEngine::run_forward(const std::unordered_set<uint64_t>& breakpoints) {
  return run_flow(true, breakpoints);
}

model::ReplayUpdate RewindEngine::run_backward(const std::unordered_set<uint64_t>& breakpoints) {
  return run_flow(false, breakpoints);
}

model::ReplayUpdate RewindEngine::run_to_address(uint64_t trace_address, bool forward) {
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!ensure_position(error)) {
    return make_status_update(error);
  }
  if (!fast_cursor_) {
    return make_error_update("Flow cursor unavailable");
  }

  if (current_step_.address == trace_address) {
    return make_status_update("At target");
  }

  std::unordered_set<uint64_t> targets;
  targets.insert(trace_address);

  if (!current_step_.is_block) {
    std::string hit_error;
    auto immediate = find_breakpoint_in_current_block(targets, forward, hit_error);
    if (immediate.has_value()) {
      if (!seek_to_address(*immediate, forward, error)) {
        return make_error_update(error);
      }
      model::ReplayUpdate update{};
      update.status = "Reached target";
      update_builder_.fill_update(make_update_context(), update);
      return update;
    }
  }

  if (!fast_cursor_->seek(current_thread_, current_step_.sequence)) {
    return make_error_update(std::string(fast_cursor_->error()));
  }

  w1::rewind::flow_step step{};
  if (!fast_cursor_->step_forward(step)) {
    auto kind = fast_cursor_->error_kind();
    if (forward && kind == w1::rewind::flow_error_kind::end_of_trace) {
      return make_status_update("End of trace");
    }
    if (!forward && kind == w1::rewind::flow_error_kind::begin_of_trace) {
      return make_status_update("Start of trace");
    }
    return make_error_update(std::string(fast_cursor_->error()));
  }

  cancel_requested_.store(false);
  run_active_.store(true);

  std::optional<w1::rewind::flow_step> last_step;
  std::optional<uint64_t> hit_address;
  std::string stop_reason;
  for (;;) {
    if (cancel_requested_.load()) {
      stop_reason = "Paused";
      break;
    }

    bool ok = forward ? fast_cursor_->step_forward(step) : fast_cursor_->step_backward(step);
    if (!ok) {
      auto kind = fast_cursor_->error_kind();
      if (forward && kind == w1::rewind::flow_error_kind::end_of_trace) {
        stop_reason = "End of trace";
      } else if (!forward && kind == w1::rewind::flow_error_kind::begin_of_trace) {
        stop_reason = "Start of trace";
      } else {
        stop_reason = std::string(fast_cursor_->error());
      }
      break;
    }

    last_step = step;
    std::string hit_error;
    auto hit = find_breakpoint_hit(step, targets, forward, hit_error);
    if (hit.has_value()) {
      stop_reason = "Reached target";
      hit_address = hit;
      break;
    }
  }

  run_active_.store(false);
  cancel_requested_.store(false);

  if (last_step.has_value()) {
    if (!move_to_sequence(current_thread_, last_step->sequence, error)) {
      return make_error_update(error);
    }
    if (hit_address.has_value() && current_step_.address != *hit_address) {
      if (!seek_to_address(*hit_address, true, error)) {
        return make_error_update(error);
      }
    }
  }

  model::ReplayUpdate update{};
  update.status = stop_reason.empty() ? "Stopped" : stop_reason;
  update_builder_.fill_update(make_update_context(), update);
  return update;
}

model::ReplayUpdate RewindEngine::run_to_view_address(uint64_t view_address, bool forward) {
  if (!mapper_.has_primary_mapping()) {
    return make_error_update("address mapper unavailable");
  }
  auto trace_addr = mapper_.view_to_trace(view_address, 1);
  if (!trace_addr.has_value()) {
    return make_error_update("address not mapped to trace");
  }
  return run_to_address(*trace_addr, forward);
}

model::ReplayUpdate RewindEngine::define_functions_from_trace(
    const std::function<void(model::ReplayUpdate)>& progress
) {
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!mapper_.has_primary_mapping()) {
    return make_error_update("address mapper unavailable");
  }
  if (run_active_.load()) {
    return make_error_update("pause playback before defining functions");
  }

  if (logger_) {
    logger_->LogInfo("Rewind: scanning trace for function candidates");
  }
  if (progress) {
    progress(make_status_update("Scanning trace for functions..."));
  }

  const auto result = function_definer_.define_functions(*session_, trace_index_, mapper_, view_, trace_path_, logger_);
  if (!result.error.empty()) {
    return make_error_update(result.error);
  }
  if (result.candidates == 0) {
    return make_status_update("No trace addresses mapped to view");
  }

  if (logger_) {
    logger_->LogInfo(
        "Rewind: function scan complete steps=%zu candidates=%zu created=%zu skipped=%zu no_segment=%zu",
        result.steps_scanned, result.candidates, result.created, result.skipped, result.no_segment
    );
  }

  return make_status_update("Defined " + std::to_string(result.created) + " functions from trace");
}

} // namespace binja::rewind::core::engine
