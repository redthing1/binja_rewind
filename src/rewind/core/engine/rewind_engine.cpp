#include "rewind/core/engine/rewind_engine.hpp"
#include "rewind/core/engine/run_loop.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <vector>

#include "w1base/arch_spec.hpp"
#include "w1rewind/format/trace_format.hpp"
#include "w1rewind/trace/trace_reader.hpp"

namespace binja::rewind::core::engine {

namespace {

decode::InstructionDecoder::instruction_mode instruction_mode_from_step(const w1::rewind::flow_step& step) {
  decode::InstructionDecoder::instruction_mode mode{};
  if (step.is_block) {
    if ((step.flags & w1::rewind::trace_block_flag_mode_valid) != 0) {
      mode.mode_valid = true;
      mode.thumb = (step.flags & w1::rewind::trace_block_flag_thumb) != 0;
    }
  } else {
    if ((step.flags & w1::rewind::trace_inst_flag_mode_valid) != 0) {
      mode.mode_valid = true;
      mode.thumb = (step.flags & w1::rewind::trace_inst_flag_thumb) != 0;
    }
  }
  return mode;
}

} // namespace

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
  size_t history_size = std::min(fast_history_size_, static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
  w1::rewind::record_stream_cursor stream_cursor(fast_stream);
  w1::rewind::flow_extractor extractor(&session_->context());
  w1::rewind::history_window history(history_size);
  fast_cursor_.emplace(std::move(stream_cursor), std::move(extractor), std::move(history), trace_index_);
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

        w1::rewind::decoded_block decoded{};
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
          for (size_t i = 0; i < decoded.instructions.size(); ++i) {
            if (decoded.instructions[i].address == address) {
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

  decode::InstructionDecoder::instruction_semantics semantics{};
  if (!instruction_decoder_.decode_instruction_semantics(
          current_step_.address, semantics, error, instruction_mode_from_step(current_step_)
      )) {
    if (!step_forward(error)) {
      return make_error_update(error);
    }
    return make_status_update("Stepped");
  }

  if (!semantics.is_call) {
    if (!step_forward(error)) {
      return make_error_update(error);
    }
    return make_status_update("Stepped");
  }

  uint64_t target_address = current_step_.address + semantics.length;
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

  decode::InstructionDecoder::instruction_semantics semantics{};
  if (instruction_decoder_.decode_instruction_semantics(
          current_step_.address, semantics, error, instruction_mode_from_step(current_step_)
      ) &&
      semantics.is_return) {
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

    decode::InstructionDecoder::instruction_semantics step_semantics{};
    std::string decode_error;
    if (instruction_decoder_.decode_instruction_semantics(
            current_step_.address, step_semantics, decode_error, instruction_mode_from_step(current_step_)
        )) {
      if (step_semantics.is_call) {
        depth++;
      }
      if (step_semantics.is_return) {
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

  decode::InstructionDecoder::instruction_semantics semantics{};
  if (!instruction_decoder_.decode_instruction_semantics(
          current_step_.address, semantics, error, instruction_mode_from_step(current_step_)
      )) {
    return make_status_update("Stepped back");
  }

  if (semantics.is_call && !semantics.is_return) {
    return make_status_update("Step over back");
  }

  if (!semantics.is_return) {
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

    decode::InstructionDecoder::instruction_semantics step_semantics{};
    std::string decode_error;
    if (instruction_decoder_.decode_instruction_semantics(
            current_step_.address, step_semantics, decode_error, instruction_mode_from_step(current_step_)
        )) {
      if (step_semantics.is_return) {
        depth++;
      }
      if (step_semantics.is_call) {
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

    decode::InstructionDecoder::instruction_semantics step_semantics{};
    std::string decode_error;
    if (instruction_decoder_.decode_instruction_semantics(
            current_step_.address, step_semantics, decode_error, instruction_mode_from_step(current_step_)
        )) {
      if (step_semantics.is_return) {
        depth++;
      }
      if (step_semantics.is_call) {
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

  std::optional<breakpoint_skip> skip_breakpoint;
  if (breakpoints.find(current_step_.address) != breakpoints.end()) {
    skip_breakpoint = breakpoint_skip{current_step_.address, current_step_.sequence};
    if (logger_) {
      logger_->LogDebug(
          "Rewind: skipping current breakpoint at 0x%llx for continue",
          static_cast<unsigned long long>(skip_breakpoint->address)
      );
    }
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
    auto immediate = breakpoint_matcher_.match_in_current_block(
        session_->context(), current_step_, forward, breakpoints, skip_breakpoint, bp_error
    );
    if (immediate.kind == breakpoint_match_kind::exact) {
      if (!seek_to_address(immediate.address, forward, error)) {
        return make_error_update(error);
      }
      model::ReplayUpdate update{};
      update.status = "Breakpoint hit";
      update_builder_.fill_update(make_update_context(), update);
      return update;
    }
    if (immediate.kind == breakpoint_match_kind::unresolved_block) {
      if (!bp_error.empty() && logger_) {
        logger_->LogWarn(
            "Rewind: breakpoint candidate in current block 0x%llx unresolved: %s",
            static_cast<unsigned long long>(immediate.address), bp_error.c_str()
        );
      }
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

  run_loop loop(&(*fast_cursor_), &breakpoint_matcher_, &session_->context());
  auto stop = loop.run(forward, breakpoints, skip_breakpoint, is_cancelled);

  run_active_.store(false);

  std::optional<w1::rewind::flow_step> last_step = stop.last_step;
  std::optional<uint64_t> hit_address = stop.hit_address;
  bool hit_exact = false;
  std::string stop_reason;
  switch (stop.reason) {
  case run_stop_reason::hit_exact:
    stop_reason = "Breakpoint hit";
    hit_exact = true;
    break;
  case run_stop_reason::hit_in_block:
    stop_reason = "Breakpoint in block";
    if (!stop.detail.empty() && logger_ && hit_address.has_value()) {
      logger_->LogWarn(
          "Rewind: breakpoint candidate in block 0x%llx unresolved: %s", static_cast<unsigned long long>(*hit_address),
          stop.detail.c_str()
      );
    }
    break;
  case run_stop_reason::end_of_trace:
    stop_reason = "End of trace";
    break;
  case run_stop_reason::begin_of_trace:
    stop_reason = "Start of trace";
    break;
  case run_stop_reason::cancelled:
    stop_reason = "Paused";
    break;
  case run_stop_reason::error:
  default:
    stop_reason = stop.detail.empty() ? "Playback error" : stop.detail;
    break;
  }
  // cancel_epoch_ intentionally not reset; token-based cancellation is per-run.

  if (last_step.has_value()) {
    if (!move_to_sequence(current_thread_, last_step->sequence, error)) {
      return make_error_update(error);
    }
    if (hit_exact && hit_address.has_value() && (current_step_.is_block || current_step_.address != *hit_address)) {
      if (!seek_to_address(*hit_address, forward, error)) {
        return make_error_update(error);
      }
    }
    if (hit_exact && hit_address.has_value() && current_step_.is_block) {
      if (!seek_to_address(*hit_address, true, error)) {
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
    logger_->LogDebug(
        "Rewind: stop position seq=%llu addr=0x%llx is_block=%s",
        static_cast<unsigned long long>(current_step_.sequence), static_cast<unsigned long long>(current_step_.address),
        current_step_.is_block ? "true" : "false"
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
    auto immediate = breakpoint_matcher_.match_in_current_block(
        session_->context(), current_step_, forward, targets, std::nullopt, hit_error
    );
    if (immediate.kind == breakpoint_match_kind::exact) {
      if (!seek_to_address(immediate.address, forward, error)) {
        return make_error_update(error);
      }
      model::ReplayUpdate update{};
      update.status = "Reached target";
      update_builder_.fill_update(make_update_context(), update);
      return update;
    }
    if (immediate.kind == breakpoint_match_kind::unresolved_block) {
      if (!hit_error.empty() && logger_) {
        logger_->LogWarn(
            "Rewind: target candidate in current block 0x%llx unresolved: %s",
            static_cast<unsigned long long>(immediate.address), hit_error.c_str()
        );
      }
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

  run_loop loop(&(*fast_cursor_), &breakpoint_matcher_, &session_->context());
  auto stop = loop.run(forward, targets, std::nullopt, is_cancelled);

  run_active_.store(false);

  std::optional<w1::rewind::flow_step> last_step = stop.last_step;
  std::optional<uint64_t> hit_address = stop.hit_address;
  bool hit_exact = false;
  std::string stop_reason;
  switch (stop.reason) {
  case run_stop_reason::hit_exact:
    stop_reason = "Reached target";
    hit_exact = true;
    break;
  case run_stop_reason::hit_in_block:
    stop_reason = "Target in block";
    if (!stop.detail.empty() && logger_ && hit_address.has_value()) {
      logger_->LogWarn(
          "Rewind: target candidate in block 0x%llx unresolved: %s", static_cast<unsigned long long>(*hit_address),
          stop.detail.c_str()
      );
    }
    break;
  case run_stop_reason::end_of_trace:
    stop_reason = "End of trace";
    break;
  case run_stop_reason::begin_of_trace:
    stop_reason = "Start of trace";
    break;
  case run_stop_reason::cancelled:
    stop_reason = "Paused";
    break;
  case run_stop_reason::error:
  default:
    stop_reason = stop.detail.empty() ? "Playback error" : stop.detail;
    break;
  }
  // cancel_epoch_ intentionally not reset; token-based cancellation is per-run.

  if (last_step.has_value()) {
    if (!move_to_sequence(current_thread_, last_step->sequence, error)) {
      return make_error_update(error);
    }
    if (hit_exact && hit_address.has_value() && (current_step_.is_block || current_step_.address != *hit_address)) {
      if (!seek_to_address(*hit_address, forward, error)) {
        return make_error_update(error);
      }
    }
    if (hit_exact && hit_address.has_value() && current_step_.is_block) {
      if (!seek_to_address(*hit_address, true, error)) {
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
    logger_->LogDebug(
        "Rewind: stop position seq=%llu addr=0x%llx is_block=%s",
        static_cast<unsigned long long>(current_step_.sequence), static_cast<unsigned long long>(current_step_.address),
        current_step_.is_block ? "true" : "false"
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

model::ReplayUpdate RewindEngine::run_function_discovery_analysis(
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
    return make_error_update("pause playback before function discovery analysis");
  }

  if (logger_) {
    logger_->LogInfo("Rewind: scanning trace for function discovery analysis");
  }
  if (progress) {
    progress(make_status_update("Scanning trace for function discovery analysis..."));
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
        "Rewind: function discovery analysis complete steps=%zu candidates=%zu created=%zu skipped=%zu no_segment=%zu",
        result.steps_scanned, result.candidates, result.created, result.skipped, result.no_segment
    );
  }

  return make_status_update("Defined " + std::to_string(result.created) + " functions from trace analysis");
}

model::ReplayUpdate RewindEngine::run_control_flow_edge_analysis(
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
    return make_error_update("pause playback before control flow edge analysis");
  }

  if (logger_) {
    logger_->LogInfo("Rewind: scanning trace for control flow edge analysis");
  }
  if (progress) {
    progress(make_status_update("Scanning trace for control flow edge analysis..."));
  }

  const uint64_t cancel_token = cancel_epoch_.load(std::memory_order_relaxed);
  auto is_cancelled = [this, cancel_token]() { return cancel_epoch_.load(std::memory_order_relaxed) != cancel_token; };
  auto progress_status = [&progress, this](const std::string& status) {
    if (progress) {
      progress(make_status_update(status));
    }
  };

  const auto result = control_flow_analyzer_.add_control_flow_edges(
      *session_, trace_index_, mapper_, view_, trace_path_, logger_, progress_status, is_cancelled
  );
  if (!result.error.empty()) {
    if (result.error == "cancelled") {
      return make_status_update("Cancelled");
    }
    return make_error_update(result.error);
  }

  if (logger_) {
    logger_->LogInfo(
        "Rewind: control flow edge analysis complete steps=%zu transitions=%zu edges=%zu added=%zu "
        "skipped_no_branch=%zu skipped_no_mapping=%zu skipped_decode=%zu skipped_no_function=%zu "
        "skipped_no_segment=%zu skipped_existing=%zu skipped_return_gap=%zu",
        result.steps_scanned, result.transitions, result.edges_found, result.edges_added,
        result.edges_skipped_no_branch, result.edges_skipped_no_mapping, result.edges_skipped_decode,
        result.edges_skipped_no_function, result.edges_skipped_no_segment, result.edges_skipped_existing,
        result.edges_skipped_return_gap
    );
  }

  if (result.edges_found == 0) {
    return make_status_update("No control flow edges found");
  }
  if (result.edges_added == 0) {
    return make_status_update("No new control flow edges added");
  }

  return make_status_update("Added " + std::to_string(result.edges_added) + " control flow edges");
}

} // namespace binja::rewind::core::engine
