#include "rewind/core/engine/rewind_engine.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
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

bool RewindEngine::ensure_instruction_position(bool forward, std::string& error) {
  if (!current_step_.is_block) {
    return true;
  }
  if (!session_.has_value()) {
    error = "session not ready";
    return false;
  }
  if (!session_->sync_instruction_position(forward)) {
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
  cursor_config.history_size =
      static_cast<uint32_t>(std::min(fast_history_size_, static_cast<size_t>(std::numeric_limits<uint32_t>::max())));
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
  if (!ensure_instruction_position(forward, error)) {
    return false;
  }

  if (current_step_.address == trace_address) {
    return true;
  }

  const auto& context = session_->context();
  if (context.has_blocks() && current_step_.block_id != 0) {
    auto it = context.blocks_by_id.find(current_step_.block_id);
    if (it != context.blocks_by_id.end()) {
      const uint64_t block_start = it->second.address;
      const uint64_t block_end = block_start + it->second.size;
      if (trace_address >= block_start && trace_address < block_end) {
        if (current_step_.is_block) {
          error = "block decode unavailable";
          if (logger_) {
            logger_->LogWarn(
                "Rewind: cannot seek within block 0x%llx; instruction position unavailable",
                static_cast<unsigned long long>(block_start)
            );
          }
          return false;
        }
        w1::rewind::flow_step block_step = current_step_;
        block_step.is_block = true;
        block_step.address = block_start;
        block_step.size = it->second.size;

        w1::rewind::replay_decoded_block decoded{};
        std::string decode_error;
        if (!block_decoder_.decode_block(context, block_step, decoded, decode_error)) {
          error = decode_error.empty() ? "block decode failed" : decode_error;
          if (logger_) {
            logger_->LogWarn(
                "Rewind: failed to decode block 0x%llx for intra-block seek: %s",
                static_cast<unsigned long long>(block_start), error.c_str()
            );
          }
          return false;
        }

        auto find_index = [&](uint64_t address, size_t& out_index) {
          if (address < decoded.address) {
            return false;
          }
          uint64_t offset = address - decoded.address;
          for (size_t i = 0; i < decoded.instructions.size(); ++i) {
            if (decoded.instructions[i].offset == offset) {
              out_index = i;
              return true;
            }
          }
          return false;
        };

        size_t current_index = 0;
        if (!find_index(current_step_.address, current_index)) {
          error = "current instruction not found in decoded block";
          if (logger_) {
            logger_->LogWarn(
                "Rewind: current instruction not found in decoded block 0x%llx",
                static_cast<unsigned long long>(block_start)
            );
          }
          return false;
        }
        size_t target_index = 0;
        if (!find_index(trace_address, target_index)) {
          error = "target address not found in decoded block";
          if (logger_) {
            logger_->LogWarn(
                "Rewind: target address 0x%llx not found in decoded block 0x%llx",
                static_cast<unsigned long long>(trace_address), static_cast<unsigned long long>(block_start)
            );
          }
          return false;
        }
        if (forward && target_index < current_index) {
          error = "target is before current position in block";
          if (logger_) {
            logger_->LogWarn(
                "Rewind: target address 0x%llx before current instruction in block 0x%llx",
                static_cast<unsigned long long>(trace_address), static_cast<unsigned long long>(block_start)
            );
          }
          return false;
        }
        if (!forward && target_index > current_index) {
          error = "target is after current position in block";
          if (logger_) {
            logger_->LogWarn(
                "Rewind: target address 0x%llx after current instruction in block 0x%llx",
                static_cast<unsigned long long>(trace_address), static_cast<unsigned long long>(block_start)
            );
          }
          return false;
        }
        size_t steps_needed = forward ? (target_index - current_index) : (current_index - target_index);
        for (size_t i = 0; i < steps_needed; ++i) {
          bool ok = forward ? session_->step_instruction() : session_->step_instruction_backward();
          if (!ok) {
            error = session_->error();
            return false;
          }
          if (!update_current_step(error)) {
            return false;
          }
        }
        if (logger_) {
          logger_->LogDebug(
              "Rewind: intra-block seek %s to 0x%llx in block 0x%llx steps=%zu", forward ? "forward" : "backward",
              static_cast<unsigned long long>(trace_address), static_cast<unsigned long long>(block_start), steps_needed
          );
        }
        return current_step_.address == trace_address;
      }
    }
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
  if (logger_) {
    logger_->LogWarn(
        "Rewind: seek to 0x%llx %s exceeded step limit %zu (current 0x%llx)",
        static_cast<unsigned long long>(trace_address), forward ? "forward" : "backward", max_steps,
        static_cast<unsigned long long>(current_step_.address)
    );
  }
  return false;
}

model::ReplayUpdate RewindEngine::load_trace(const std::string& path) {
  request_cancel();
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
  request_cancel();
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
  if (!ensure_instruction_position(forward, error)) {
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
  if (!ensure_instruction_position(true, error)) {
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
  if (!ensure_instruction_position(true, error)) {
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
  if (!ensure_instruction_position(false, error)) {
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
  if (!ensure_instruction_position(false, error)) {
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
  request_cancel();
  model::ReplayUpdate update{};
  update.status = "Paused";
  update.status_only = true;
  return update;
}

void RewindEngine::request_cancel() { cancel_epoch_.fetch_add(1, std::memory_order_relaxed); }

void RewindEngine::set_gradient_size(size_t size) {
  size_t clamped = size;
  if (clamped < 1) {
    clamped = 1;
  } else if (clamped > 64) {
    clamped = 64;
  }
  gradient_size_ = clamped;
}

void RewindEngine::set_reverse_history_size(size_t size) {
  size_t clamped = size < 1 ? 1 : size;
  size_t max_size = static_cast<size_t>(std::numeric_limits<uint32_t>::max());
  if (clamped > max_size) {
    clamped = max_size;
  }
  fast_history_size_ = clamped;
  if (fast_cursor_) {
    fast_cursor_->set_history_size(static_cast<uint32_t>(fast_history_size_));
  }
}

std::unordered_set<uint64_t> RewindEngine::collect_breakpoints() const {
  return breakpoint_provider_.collect_breakpoints(view_, mapper_);
}

RewindEngine::BreakpointResult RewindEngine::find_breakpoint_in_current_block(
    const std::unordered_set<uint64_t>& breakpoints, bool forward, std::string& error
) {
  BreakpointResult result{};
  error.clear();
  if (breakpoints.empty() || !session_.has_value() || !has_position_) {
    return result;
  }
  const auto& context = session_->context();
  if (!context.has_blocks()) {
    return result;
  }
  if (current_step_.block_id == 0) {
    return result;
  }

  auto it = context.blocks_by_id.find(current_step_.block_id);
  if (it == context.blocks_by_id.end()) {
    return result;
  }

  w1::rewind::flow_step block_step = current_step_;
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
    if (bp >= block_start && bp < block_end) {
      candidate = true;
      break;
    }
  }
  if (!candidate) {
    return result;
  }

  w1::rewind::replay_decoded_block decoded{};
  if (!block_decoder_.decode_block(context, block_step, decoded, error)) {
    if (logger_) {
      logger_->LogWarn(
          "Rewind: breakpoint candidate in block 0x%llx decode failed: %s",
          static_cast<unsigned long long>(block_step.address), error.c_str()
      );
    }
    result.kind = BreakpointResult::Kind::unresolved_block;
    result.address = block_step.address;
    return result;
  }

  size_t current_index = decoded.instructions.size();
  if (current_step_.is_block) {
    current_index = forward ? 0 : decoded.instructions.size();
  } else {
    for (size_t i = 0; i < decoded.instructions.size(); ++i) {
      uint64_t addr = decoded.address + decoded.instructions[i].offset;
      if (addr == current_step_.address) {
        current_index = i;
        break;
      }
    }
    if (current_index == decoded.instructions.size()) {
      return result;
    }
  }

  if (forward) {
    size_t start = current_step_.is_block ? current_index : current_index + 1;
    for (size_t i = start; i < decoded.instructions.size(); ++i) {
      uint64_t addr = decoded.address + decoded.instructions[i].offset;
      if (breakpoints.find(addr) != breakpoints.end()) {
        result.kind = BreakpointResult::Kind::exact;
        result.address = addr;
        return result;
      }
    }
  } else {
    size_t start = current_index;
    for (size_t i = start; i-- > 0;) {
      uint64_t addr = decoded.address + decoded.instructions[i].offset;
      if (breakpoints.find(addr) != breakpoints.end()) {
        result.kind = BreakpointResult::Kind::exact;
        result.address = addr;
        return result;
      }
    }
  }

  error = "breakpoint address not found in decoded block";
  if (logger_) {
    logger_->LogWarn(
        "Rewind: breakpoint candidate in block 0x%llx not found after decode",
        static_cast<unsigned long long>(block_step.address)
    );
  }
  result.kind = BreakpointResult::Kind::unresolved_block;
  result.address = block_step.address;
  return result;
}

RewindEngine::BreakpointResult RewindEngine::find_breakpoint_hit(
    const w1::rewind::flow_step& step, const std::unordered_set<uint64_t>& breakpoints, bool forward, std::string& error
) {
  BreakpointResult result{};
  error.clear();
  if (breakpoints.empty()) {
    return result;
  }
  if (!session_.has_value()) {
    return result;
  }
  const auto& context = session_->context();
  if (!context.has_blocks()) {
    if (breakpoints.find(step.address) != breakpoints.end()) {
      result.kind = BreakpointResult::Kind::exact;
      result.address = step.address;
      return result;
    }
    return result;
  }

  if (breakpoints.find(step.address) != breakpoints.end()) {
    result.kind = BreakpointResult::Kind::exact;
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
    if (bp >= block_start && bp < block_end) {
      candidate = true;
      break;
    }
  }
  if (!candidate) {
    return result;
  }

  w1::rewind::replay_decoded_block decoded{};
  if (!block_decoder_.decode_block(context, step, decoded, error)) {
    if (logger_) {
      logger_->LogWarn(
          "Rewind: breakpoint candidate in block 0x%llx decode failed: %s",
          static_cast<unsigned long long>(step.address), error.c_str()
      );
    }
    result.kind = BreakpointResult::Kind::unresolved_block;
    result.address = step.address;
    return result;
  }

  if (forward) {
    for (const auto& inst : decoded.instructions) {
      uint64_t addr = decoded.address + inst.offset;
      if (breakpoints.find(addr) != breakpoints.end()) {
        result.kind = BreakpointResult::Kind::exact;
        result.address = addr;
        return result;
      }
    }
  } else {
    for (auto it = decoded.instructions.rbegin(); it != decoded.instructions.rend(); ++it) {
      uint64_t addr = decoded.address + it->offset;
      if (breakpoints.find(addr) != breakpoints.end()) {
        result.kind = BreakpointResult::Kind::exact;
        result.address = addr;
        return result;
      }
    }
  }

  error = "breakpoint address not found in decoded block";
  if (logger_) {
    logger_->LogWarn(
        "Rewind: breakpoint candidate in block 0x%llx not found after decode",
        static_cast<unsigned long long>(step.address)
    );
  }
  result.kind = BreakpointResult::Kind::unresolved_block;
  result.address = step.address;
  return result;
}

model::ReplayUpdate RewindEngine::run_flow(bool forward, const std::unordered_set<uint64_t>& breakpoints) {
  const uint64_t cancel_token = cancel_epoch_.load(std::memory_order_relaxed);
  auto is_cancelled = [this, cancel_token]() { return cancel_epoch_.load(std::memory_order_relaxed) != cancel_token; };
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!ensure_position(error)) {
    return make_status_update(error);
  }
  if (!ensure_instruction_position(forward, error)) {
    return make_error_update(error);
  }
  if (!fast_cursor_) {
    return make_error_update("Flow cursor unavailable");
  }

  if (logger_) {
    logger_->LogDebug(
        "Rewind: run %s from seq=%llu addr=0x%llx thread=%llu breakpoints=%zu", forward ? "forward" : "backward",
        static_cast<unsigned long long>(current_step_.sequence), static_cast<unsigned long long>(current_step_.address),
        static_cast<unsigned long long>(current_thread_), breakpoints.size()
    );
  }

  fast_cursor_->set_cancel_checker([is_cancelled]() { return is_cancelled(); });
  struct CancelReset {
    w1::rewind::flow_cursor* cursor = nullptr;
    ~CancelReset() {
      if (cursor) {
        cursor->set_cancel_checker({});
      }
    }
  } cancel_reset{&(*fast_cursor_)};

  {
    std::string bp_error;
    auto immediate = find_breakpoint_in_current_block(breakpoints, forward, bp_error);
    if (immediate.kind == BreakpointResult::Kind::exact) {
      if (!seek_to_address(immediate.address, forward, error)) {
        return make_error_update(error);
      }
      model::ReplayUpdate update{};
      update.status = "Breakpoint hit";
      update_builder_.fill_update(make_update_context(), update);
      return update;
    }
    if (immediate.kind == BreakpointResult::Kind::unresolved_block) {
      if (logger_) {
        logger_->LogDebug(
            "Rewind: breakpoint candidate in current block 0x%llx unresolved; continuing",
            static_cast<unsigned long long>(immediate.address)
        );
      }
    }
  }

  if (!fast_cursor_->seek(current_thread_, current_step_.sequence)) {
    return make_error_update(std::string(fast_cursor_->error()));
  }

  w1::rewind::flow_step step{};
  if (!fast_cursor_->step_forward(step)) {
    if (is_cancelled()) {
      return make_status_update("Paused");
    }
    auto kind = fast_cursor_->error_kind();
    if (kind == w1::rewind::flow_error_kind::end_of_trace) {
      return make_status_update("End of trace");
    }
    return make_error_update(std::string(fast_cursor_->error()));
  }

  run_active_.store(true);

  std::optional<w1::rewind::flow_step> last_step;
  std::optional<uint64_t> hit_address;
  bool hit_exact = false;
  std::string stop_reason;
  for (;;) {
    if (is_cancelled()) {
      stop_reason = "Paused";
      break;
    }

    bool ok = forward ? fast_cursor_->step_forward(step) : fast_cursor_->step_backward(step);
    if (!ok) {
      if (is_cancelled()) {
        stop_reason = "Paused";
      } else {
        auto kind = fast_cursor_->error_kind();
        if (forward && kind == w1::rewind::flow_error_kind::end_of_trace) {
          stop_reason = "End of trace";
        } else if (!forward && kind == w1::rewind::flow_error_kind::begin_of_trace) {
          stop_reason = "Start of trace";
        } else {
          stop_reason = std::string(fast_cursor_->error());
        }
      }
      break;
    }

    last_step = step;
    std::string bp_error;
    auto hit = find_breakpoint_hit(step, breakpoints, forward, bp_error);
    if (hit.kind == BreakpointResult::Kind::exact) {
      stop_reason = "Breakpoint hit";
      hit_address = hit.address;
      hit_exact = true;
      break;
    }
    if (hit.kind == BreakpointResult::Kind::unresolved_block) {
      stop_reason = "Breakpoint in block";
      hit_address = hit.address;
      hit_exact = false;
      break;
    }
  }

  run_active_.store(false);
  // cancel_epoch_ intentionally not reset; token-based cancellation is per-run.

  if (last_step.has_value()) {
    if (!move_to_sequence(current_thread_, last_step->sequence, error)) {
      return make_error_update(error);
    }
    if (hit_exact && hit_address.has_value() && current_step_.address != *hit_address) {
      if (!seek_to_address(*hit_address, forward, error)) {
        return make_error_update(error);
      }
    }
  }

  if (logger_) {
    const auto seq = last_step.has_value() ? last_step->sequence : current_step_.sequence;
    const auto addr = last_step.has_value() ? last_step->address : current_step_.address;
    logger_->LogDebug(
        "Rewind: run %s stopped reason='%s' seq=%llu addr=0x%llx hit=0x%llx exact=%s", forward ? "forward" : "backward",
        stop_reason.empty() ? "stopped" : stop_reason.c_str(), static_cast<unsigned long long>(seq),
        static_cast<unsigned long long>(addr), static_cast<unsigned long long>(hit_address.value_or(0)),
        hit_exact ? "true" : "false"
    );
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
  const uint64_t cancel_token = cancel_epoch_.load(std::memory_order_relaxed);
  auto is_cancelled = [this, cancel_token]() { return cancel_epoch_.load(std::memory_order_relaxed) != cancel_token; };
  std::string error;
  if (!ensure_session_ready(error)) {
    return make_error_update(error);
  }
  if (!ensure_position(error)) {
    return make_status_update(error);
  }
  if (!ensure_instruction_position(forward, error)) {
    return make_error_update(error);
  }
  if (!fast_cursor_) {
    return make_error_update("Flow cursor unavailable");
  }

  if (logger_) {
    logger_->LogDebug(
        "Rewind: run to 0x%llx %s from seq=%llu addr=0x%llx thread=%llu",
        static_cast<unsigned long long>(trace_address), forward ? "forward" : "backward",
        static_cast<unsigned long long>(current_step_.sequence), static_cast<unsigned long long>(current_step_.address),
        static_cast<unsigned long long>(current_thread_)
    );
  }

  fast_cursor_->set_cancel_checker([is_cancelled]() { return is_cancelled(); });
  struct CancelReset {
    w1::rewind::flow_cursor* cursor = nullptr;
    ~CancelReset() {
      if (cursor) {
        cursor->set_cancel_checker({});
      }
    }
  } cancel_reset{&(*fast_cursor_)};

  if (current_step_.address == trace_address) {
    return make_status_update("At target");
  }

  std::unordered_set<uint64_t> targets;
  targets.insert(trace_address);

  {
    std::string hit_error;
    auto immediate = find_breakpoint_in_current_block(targets, forward, hit_error);
    if (immediate.kind == BreakpointResult::Kind::exact) {
      if (!seek_to_address(immediate.address, forward, error)) {
        return make_error_update(error);
      }
      model::ReplayUpdate update{};
      update.status = "Reached target";
      update_builder_.fill_update(make_update_context(), update);
      return update;
    }
    if (immediate.kind == BreakpointResult::Kind::unresolved_block) {
      if (logger_) {
        logger_->LogDebug(
            "Rewind: target candidate in current block 0x%llx unresolved; stopping",
            static_cast<unsigned long long>(immediate.address)
        );
      }
      model::ReplayUpdate update{};
      update.status = "Target in block";
      update_builder_.fill_update(make_update_context(), update);
      return update;
    }
  }

  if (!fast_cursor_->seek(current_thread_, current_step_.sequence)) {
    return make_error_update(std::string(fast_cursor_->error()));
  }

  w1::rewind::flow_step step{};
  if (!fast_cursor_->step_forward(step)) {
    if (is_cancelled()) {
      return make_status_update("Paused");
    }
    auto kind = fast_cursor_->error_kind();
    if (forward && kind == w1::rewind::flow_error_kind::end_of_trace) {
      return make_status_update("End of trace");
    }
    if (!forward && kind == w1::rewind::flow_error_kind::begin_of_trace) {
      return make_status_update("Start of trace");
    }
    return make_error_update(std::string(fast_cursor_->error()));
  }

  run_active_.store(true);

  std::optional<w1::rewind::flow_step> last_step;
  std::optional<uint64_t> hit_address;
  bool hit_exact = false;
  std::string stop_reason;
  for (;;) {
    if (is_cancelled()) {
      stop_reason = "Paused";
      break;
    }

    bool ok = forward ? fast_cursor_->step_forward(step) : fast_cursor_->step_backward(step);
    if (!ok) {
      if (is_cancelled()) {
        stop_reason = "Paused";
      } else {
        auto kind = fast_cursor_->error_kind();
        if (forward && kind == w1::rewind::flow_error_kind::end_of_trace) {
          stop_reason = "End of trace";
        } else if (!forward && kind == w1::rewind::flow_error_kind::begin_of_trace) {
          stop_reason = "Start of trace";
        } else {
          stop_reason = std::string(fast_cursor_->error());
        }
      }
      break;
    }

    last_step = step;
    std::string hit_error;
    auto hit = find_breakpoint_hit(step, targets, forward, hit_error);
    if (hit.kind == BreakpointResult::Kind::exact) {
      stop_reason = "Reached target";
      hit_address = hit.address;
      hit_exact = true;
      break;
    }
    if (hit.kind == BreakpointResult::Kind::unresolved_block) {
      stop_reason = "Target in block";
      hit_address = hit.address;
      hit_exact = false;
      break;
    }
  }

  run_active_.store(false);
  // cancel_epoch_ intentionally not reset; token-based cancellation is per-run.

  if (last_step.has_value()) {
    if (!move_to_sequence(current_thread_, last_step->sequence, error)) {
      return make_error_update(error);
    }
    if (hit_exact && hit_address.has_value() && current_step_.address != *hit_address) {
      if (!seek_to_address(*hit_address, forward, error)) {
        return make_error_update(error);
      }
    }
  }

  if (logger_) {
    const auto seq = last_step.has_value() ? last_step->sequence : current_step_.sequence;
    const auto addr = last_step.has_value() ? last_step->address : current_step_.address;
    logger_->LogDebug(
        "Rewind: run to 0x%llx %s stopped reason='%s' seq=%llu addr=0x%llx exact=%s",
        static_cast<unsigned long long>(trace_address), forward ? "forward" : "backward",
        stop_reason.empty() ? "stopped" : stop_reason.c_str(), static_cast<unsigned long long>(seq),
        static_cast<unsigned long long>(addr), hit_exact ? "true" : "false"
    );
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
