#include "rewind_core/worker.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

#include "w1base/arch_spec.hpp"
namespace binja_rewind {

namespace {

std::string format_register_value(uint64_t value, uint16_t bits) {
  std::ostringstream oss;
  unsigned width = bits ? static_cast<unsigned>((bits + 3) / 4) : 16;
  if (width == 0) {
    width = 1;
  }
  oss << "0x" << std::hex << std::setw(width) << std::setfill('0') << value;
  return oss.str();
}

} // namespace

bool RewindWorker::has_branch_type(const BinaryNinja::InstructionInfo& info, BNBranchType type) {
  for (size_t i = 0; i < info.branchCount; ++i) {
    if (info.branchType[i] == type) {
      return true;
    }
  }
  return false;
}

RewindWorker::RewindWorker(BinaryNinja::Ref<BinaryNinja::BinaryView> view, UpdateCallback callback)
    : view_(std::move(view)), callback_(std::move(callback)) {
  if (view_) {
    logger_ = view_->CreateLogger("Rewind");
  } else {
    logger_ = BinaryNinja::LogRegistry::CreateLogger("Rewind");
  }
  mapper_.set_logger(logger_);
  block_decoder_.set_view(view_);
  worker_ = std::thread([this]() { worker_loop(); });
}

RewindWorker::~RewindWorker() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  cancel_requested_.store(true);
  if (worker_.joinable()) {
    worker_.join();
  }
}

void RewindWorker::enqueue(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) {
      return;
    }
    queue_.push_back(std::move(task));
  }
  cv_.notify_one();
}

void RewindWorker::worker_loop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
      if (stop_) {
        return;
      }
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    if (task) {
      task();
    }
  }
}

void RewindWorker::post_update(ReplayUpdate update) {
  if (!callback_) {
    return;
  }
  BinaryNinja::ExecuteOnMainThread([cb = callback_, update = std::move(update)]() mutable { cb(update); });
}

void RewindWorker::post_status(const std::string& status, bool controls_enabled) {
  ReplayUpdate update{};
  update.status = status;
  update.trace_path = trace_path_;
  update.trace_loaded = trace_loaded_;
  update.controls_enabled = controls_enabled;
  fill_update(update);
  post_update(std::move(update));
}

void RewindWorker::post_update_status(const std::string& status) {
  ReplayUpdate update{};
  update.status = status;
  fill_update(update);
  post_update(std::move(update));
}

void RewindWorker::post_error(const std::string& error) { post_status("Error: " + error, controls_enabled_); }

bool RewindWorker::ensure_session_ready(std::string& error) {
  if (!trace_loaded_ || !session_.has_value()) {
    error = "trace not loaded";
    return false;
  }
  return true;
}

bool RewindWorker::ensure_position(std::string& error) const {
  if (!has_position_) {
    error = "No current position";
    return false;
  }
  return true;
}

bool RewindWorker::ensure_instruction_position(std::string& error) {
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

bool RewindWorker::open_trace(const std::string& path, std::string& error, std::string& warning) {
  close_trace();
  warning.clear();

  trace_path_ = path;
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

  trace_summary_ = TraceSummary{};
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
    TraceModule info{};
    info.path = module.path;
    info.base = module.base;
    info.size = module.size;
    info.permissions = static_cast<uint32_t>(module.permissions);
    trace_modules_.push_back(std::move(info));
  }
  trace_info_dirty_ = true;

  w1::rewind::replay_session_config config{};
  config.trace_path = path;
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

  threads_.clear();
  for (const auto& thread : session_->threads()) {
    ThreadInfo info{};
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

  w1::rewind::replay_flow_cursor_config cursor_config{};
  cursor_config.trace_path = path;
  cursor_config.index_path = session_->resolved_index_path();
  cursor_config.history_size = 4096;
  cursor_config.track_registers = false;
  cursor_config.track_memory = false;
  cursor_config.context = &session_->context();

  fast_cursor_.emplace(cursor_config);
  if (!fast_cursor_->open()) {
    error = fast_cursor_->error();
    fast_cursor_.reset();
    return false;
  }

  trace_loaded_ = true;
  controls_enabled_ = true;
  return true;
}

void RewindWorker::close_trace() {
  session_.reset();
  fast_cursor_.reset();
  threads_.clear();
  trace_summary_ = TraceSummary{};
  trace_modules_.clear();
  trace_info_dirty_ = true;
  current_thread_ = 0;
  has_position_ = false;
  current_step_ = w1::rewind::flow_step{};
  trace_loaded_ = false;
  controls_enabled_ = false;
}

bool RewindWorker::move_to_sequence(uint64_t thread_id, uint64_t sequence, std::string& error) {
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

bool RewindWorker::update_current_step(std::string& error) {
  if (!session_.has_value()) {
    error = "session not ready";
    return false;
  }
  current_step_ = session_->current_step();
  has_position_ = true;
  current_thread_ = current_step_.thread_id;
  return true;
}

bool RewindWorker::seek_to_address(uint64_t trace_address, bool forward, std::string& error, size_t max_steps) {
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

void RewindWorker::step_instruction_impl(bool forward) {
  std::string error;
  if (!ensure_session_ready(error)) {
    post_error(error);
    return;
  }
  if (!ensure_instruction_position(error)) {
    post_error(error);
    return;
  }

  bool ok = forward ? session_->step_instruction() : session_->step_instruction_backward();
  if (!ok) {
    post_error(session_->error());
    return;
  }

  std::string notice;
  if (auto session_notice = session_->take_notice(); session_notice.has_value()) {
    notice = session_notice->message;
  }

  if (!update_current_step(error)) {
    post_error(error);
    return;
  }

  ReplayUpdate update{};
  if (!notice.empty()) {
    update.status = notice;
  } else {
    update.status = forward ? "Stepped" : "Stepped back";
  }
  fill_update(update);
  post_update(std::move(update));
}

void RewindWorker::step_flow_impl(bool forward) {
  std::string error;
  if (!ensure_session_ready(error)) {
    post_error(error);
    return;
  }

  bool ok = forward ? session_->step_flow() : session_->step_backward();
  if (!ok) {
    post_error(session_->error());
    return;
  }
  if (!update_current_step(error)) {
    post_error(error);
    return;
  }

  ReplayUpdate update{};
  update.status = forward ? "Stepped" : "Stepped back";
  fill_update(update);
  post_update(std::move(update));
}

void RewindWorker::run_flow_impl(bool forward) {
  std::string error;
  if (!ensure_session_ready(error)) {
    post_error(error);
    return;
  }
  if (!ensure_position(error)) {
    post_update_status(error);
    return;
  }
  if (!fast_cursor_) {
    post_error("Flow cursor unavailable");
    return;
  }

  std::unordered_set<uint64_t> breakpoints;
  BinaryNinja::ExecuteOnMainThreadAndWait([&]() {
    breakpoints = breakpoint_provider_.collect_breakpoints(view_, mapper_);
  });

  if (!current_step_.is_block) {
    std::string bp_error;
    auto immediate = find_breakpoint_in_current_block(breakpoints, forward, bp_error);
    if (immediate.has_value()) {
      if (!seek_to_address(*immediate, forward, error)) {
        post_error(error);
        return;
      }
      ReplayUpdate update{};
      update.status = "Breakpoint hit";
      fill_update(update);
      post_update(std::move(update));
      return;
    }
  }

  if (!fast_cursor_->seek(current_thread_, current_step_.sequence)) {
    post_error(fast_cursor_->error());
    return;
  }

  w1::rewind::flow_step step{};
  if (!fast_cursor_->step_forward(step)) {
    auto kind = fast_cursor_->error_kind();
    if (kind == w1::rewind::replay_flow_error_kind::end_of_trace) {
      post_update_status("End of trace");
    } else {
      post_error(fast_cursor_->error());
    }
    return;
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
      if (forward && kind == w1::rewind::replay_flow_error_kind::end_of_trace) {
        stop_reason = "End of trace";
      } else if (!forward && kind == w1::rewind::replay_flow_error_kind::begin_of_trace) {
        stop_reason = "Start of trace";
      } else {
        stop_reason = fast_cursor_->error();
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
      post_error(error);
      return;
    }
    if (hit_address.has_value() && current_step_.address != *hit_address) {
      if (!seek_to_address(*hit_address, true, error)) {
        post_error(error);
        return;
      }
    }
  }

  ReplayUpdate update{};
  update.status = stop_reason.empty() ? "Stopped" : stop_reason;
  fill_update(update);
  post_update(std::move(update));
}

bool RewindWorker::decode_instruction(
    uint64_t trace_address, BinaryNinja::InstructionInfo& info, size_t& length, std::string& error
) {
  error.clear();
  length = 0;

  if (!view_) {
    error = "binary view unavailable";
    return false;
  }
  if (!mapper_.has_primary_mapping()) {
    error = "address mapper unavailable";
    return false;
  }

  auto view_addr = mapper_.trace_to_view(trace_address, 1);
  if (!view_addr.has_value()) {
    error = "address not mapped to binary view";
    return false;
  }

  auto arch = view_->GetDefaultArchitecture();
  if (!arch) {
    error = "binary view architecture unavailable";
    return false;
  }

  size_t max_len = arch->GetMaxInstructionLength();
  if (max_len == 0) {
    max_len = 16;
  }

  std::vector<uint8_t> buffer(max_len);
  size_t read = view_->Read(buffer.data(), *view_addr, buffer.size());
  if (read == 0) {
    error = "failed to read instruction bytes";
    return false;
  }

  BinaryNinja::InstructionInfo inst_info;
  if (!arch->GetInstructionInfo(buffer.data(), *view_addr, read, inst_info)) {
    error = "failed to decode instruction info";
    return false;
  }
  if (inst_info.length == 0 || inst_info.length > read) {
    error = "invalid instruction length";
    return false;
  }

  info = inst_info;
  length = inst_info.length;
  return true;
}

std::optional<uint64_t> RewindWorker::find_breakpoint_in_current_block(
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

void RewindWorker::fill_registers(ReplayUpdate& update) {
  if (!session_.has_value() || !has_position_) {
    return;
  }

  const auto& specs = session_->register_specs();
  const auto& names = session_->register_names();
  if (specs.empty() || names.empty()) {
    return;
  }

  auto values = session_->read_registers();
  size_t count = std::min({specs.size(), names.size(), values.size()});
  update.registers.clear();
  update.registers.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    RegisterValue entry{};
    entry.name = names[i];
    if (values[i].has_value()) {
      entry.known = true;
      entry.value = format_register_value(*values[i], specs[i].bits);
    } else {
      entry.known = false;
      entry.value = "??";
    }
    update.registers.push_back(std::move(entry));
  }
}

bool RewindWorker::sample_gradient(GradientSample& sample) {
  if (!session_.has_value() || !has_position_) {
    return false;
  }

  sample.past.clear();
  sample.future.clear();
  sample.current = current_step_.address;

  w1::rewind::replay_flow_cursor_config cfg{};
  cfg.trace_path = trace_path_;
  cfg.index_path = session_->resolved_index_path();
  cfg.history_size = static_cast<uint32_t>(gradient_size_ + 1);
  cfg.track_registers = false;
  cfg.track_memory = false;
  cfg.context = &session_->context();

  w1::rewind::replay_flow_cursor cursor(cfg);
  if (!cursor.open()) {
    return false;
  }
  if (!cursor.seek(current_thread_, current_step_.sequence)) {
    return false;
  }

  w1::rewind::flow_step flow{};
  if (!cursor.step_forward(flow)) {
    return false;
  }

  w1::rewind::replay_instruction_cursor inst(cursor);
  inst.set_decoder(&block_decoder_);
  inst.set_position(current_step_);
  sample.current = inst.current_step().address;

  w1::rewind::flow_step step = inst.current_step();
  for (size_t i = 0; i < gradient_size_; ++i) {
    if (!inst.step_backward(step)) {
      break;
    }
    sample.past.push_back(step.address);
  }

  if (!cursor.seek(current_thread_, current_step_.sequence)) {
    return true;
  }
  if (!cursor.step_forward(flow)) {
    return true;
  }
  w1::rewind::replay_instruction_cursor inst_fwd(cursor);
  inst_fwd.set_decoder(&block_decoder_);
  inst_fwd.set_position(current_step_);
  step = inst_fwd.current_step();
  for (size_t i = 0; i < gradient_size_; ++i) {
    if (!inst_fwd.step_forward(step)) {
      break;
    }
    sample.future.push_back(step.address);
  }

  return true;
}

std::optional<uint64_t> RewindWorker::find_breakpoint_hit(
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

void RewindWorker::fill_update(ReplayUpdate& update) {
  update.trace_path = trace_path_;
  update.trace_loaded = trace_loaded_;
  update.controls_enabled = controls_enabled_;

  if (has_position_) {
    update.has_position = true;
    update.thread_id = current_thread_;
    update.sequence = current_step_.sequence;

    GradientSample sample{};
    if (sample_gradient(sample)) {
      update.trace_address = sample.current;
      update.view_address = mapper_.trace_to_view(sample.current, 1);
      update.past_addresses.clear();
      update.future_addresses.clear();
      for (auto addr : sample.past) {
        if (auto mapped = mapper_.trace_to_view(addr, 1)) {
          update.past_addresses.push_back(*mapped);
        }
      }
      for (auto addr : sample.future) {
        if (auto mapped = mapper_.trace_to_view(addr, 1)) {
          update.future_addresses.push_back(*mapped);
        }
      }
    } else {
      update.trace_address = current_step_.address;
      update.view_address = mapper_.trace_to_view(current_step_.address, 1);
    }
  }

  fill_registers(update);
}

void RewindWorker::load_trace(const std::string& path) {
  enqueue([this, path]() {
    cancel_requested_.store(true);
    run_active_.store(false);

    std::string error;
    std::string warning;
    if (!open_trace(path, error, warning)) {
      if (logger_) {
        logger_->LogError("Rewind: failed to load trace '%s': %s", path.c_str(), error.c_str());
      }
      ReplayUpdate update{};
      update.status = "Error: " + error;
      update.trace_cleared = true;
      update.controls_enabled = false;
      if (trace_info_dirty_) {
        update.trace_info_changed = true;
        update.summary = trace_summary_;
        update.modules = trace_modules_;
        trace_info_dirty_ = false;
      }
      post_update(std::move(update));
      return;
    }

    ReplayUpdate update{};
    update.trace_loaded = true;
    update.controls_enabled = true;
    update.status = "Trace loaded";
    update.trace_path = trace_path_;
    update.threads = threads_;
    fill_update(update);
    if (trace_info_dirty_) {
      update.trace_info_changed = true;
      update.summary = trace_summary_;
      update.modules = trace_modules_;
      trace_info_dirty_ = false;
    }
    post_update(std::move(update));
  });
}

void RewindWorker::clear_trace() {
  enqueue([this]() {
    cancel_requested_.store(true);
    run_active_.store(false);
    close_trace();
    ReplayUpdate update{};
    update.trace_cleared = true;
    update.controls_enabled = false;
    update.status = "Trace cleared";
    if (trace_info_dirty_) {
      update.trace_info_changed = true;
      update.summary = trace_summary_;
      update.modules = trace_modules_;
      trace_info_dirty_ = false;
    }
    post_update(std::move(update));
  });
}

void RewindWorker::select_thread(uint64_t thread_id) {
  enqueue([this, thread_id]() {
    std::string error;
    if (!ensure_session_ready(error)) {
      post_error(error);
      return;
    }
    if (!move_to_sequence(thread_id, 0, error)) {
      post_error(error);
      return;
    }
    ReplayUpdate update{};
    update.status = "Thread selected";
    fill_update(update);
    post_update(std::move(update));
  });
}

void RewindWorker::step_instruction() {
  enqueue([this]() { step_instruction_impl(true); });
}

void RewindWorker::step_instruction_backward() {
  enqueue([this]() { step_instruction_impl(false); });
}

void RewindWorker::step_flow() {
  enqueue([this]() { step_flow_impl(true); });
}

void RewindWorker::step_flow_backward() {
  enqueue([this]() { step_flow_impl(false); });
}

void RewindWorker::step_over() {
  enqueue([this]() {
    std::string error;
    if (!ensure_session_ready(error)) {
      post_error(error);
      return;
    }
    if (!ensure_position(error)) {
      post_update_status(error);
      return;
    }
    if (!ensure_instruction_position(error)) {
      post_error(error);
      return;
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
    if (!decode_instruction(current_step_.address, info, length, error)) {
      if (!step_forward(error)) {
        post_error(error);
        return;
      }
      ReplayUpdate update{};
      update.status = "Stepped";
      fill_update(update);
      post_update(std::move(update));
      return;
    }

    bool is_call = has_branch_type(info, CallDestination) || has_branch_type(info, SystemCall);
    if (!is_call) {
      if (!step_forward(error)) {
        post_error(error);
        return;
      }
      ReplayUpdate update{};
      update.status = "Stepped";
      fill_update(update);
      post_update(std::move(update));
      return;
    }

    uint64_t target_address = current_step_.address + length;
    if (!seek_to_address(target_address, true, error)) {
      post_error(error);
      return;
    }

    ReplayUpdate update{};
    update.status = "Step over";
    fill_update(update);
    post_update(std::move(update));
  });
}

void RewindWorker::step_out() {
  enqueue([this]() {
    std::string error;
    if (!ensure_session_ready(error)) {
      post_error(error);
      return;
    }
    if (!ensure_position(error)) {
      post_update_status(error);
      return;
    }
    if (!ensure_instruction_position(error)) {
      post_error(error);
      return;
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
    if (decode_instruction(current_step_.address, info, length, error) && has_branch_type(info, FunctionReturn)) {
      if (!step_forward(error)) {
        post_error(error);
        return;
      }
      ReplayUpdate update{};
      update.status = "Step out";
      fill_update(update);
      post_update(std::move(update));
      return;
    }

    int depth = 0;
    size_t guard = 0;
    for (;;) {
      if (!step_forward(error)) {
        post_error(error);
        return;
      }
      if (++guard > kStepGuardLimit) {
        post_error("step out exceeded step limit");
        return;
      }

      BinaryNinja::InstructionInfo step_info{};
      size_t step_len = 0;
      std::string decode_error;
      if (decode_instruction(current_step_.address, step_info, step_len, decode_error)) {
        if (has_branch_type(step_info, CallDestination) || has_branch_type(step_info, SystemCall)) {
          depth++;
        }
        if (has_branch_type(step_info, FunctionReturn)) {
          if (depth == 0) {
            if (!step_forward(error)) {
              post_error(error);
              return;
            }
            break;
          }
          depth = std::max(0, depth - 1);
        }
      }
    }

    ReplayUpdate update{};
    update.status = "Step out";
    fill_update(update);
    post_update(std::move(update));
  });
}

void RewindWorker::step_over_backward() {
  enqueue([this]() {
    std::string error;
    if (!ensure_session_ready(error)) {
      post_error(error);
      return;
    }
    if (!ensure_position(error)) {
      post_update_status(error);
      return;
    }
    if (!ensure_instruction_position(error)) {
      post_error(error);
      return;
    }

    auto step_backward = [&](std::string& step_error) -> bool {
      if (!session_->step_instruction_backward()) {
        step_error = session_->error();
        return false;
      }
      return update_current_step(step_error);
    };

    if (!step_backward(error)) {
      post_error(error);
      return;
    }

    BinaryNinja::InstructionInfo info{};
    size_t length = 0;
    if (!decode_instruction(current_step_.address, info, length, error)) {
      ReplayUpdate update{};
      update.status = "Stepped back";
      fill_update(update);
      post_update(std::move(update));
      return;
    }

    bool is_call = has_branch_type(info, CallDestination) || has_branch_type(info, SystemCall);
    bool is_return = has_branch_type(info, FunctionReturn);

    if (is_call && !is_return) {
      ReplayUpdate update{};
      update.status = "Step over back";
      fill_update(update);
      post_update(std::move(update));
      return;
    }

    if (!is_return) {
      ReplayUpdate update{};
      update.status = "Stepped back";
      fill_update(update);
      post_update(std::move(update));
      return;
    }

    int depth = 1;
    size_t guard = 0;
    while (depth > 0) {
      if (!step_backward(error)) {
        post_error(error);
        return;
      }
      if (++guard > kStepGuardLimit) {
        post_error("step over back exceeded step limit");
        return;
      }

      BinaryNinja::InstructionInfo step_info{};
      size_t step_len = 0;
      std::string decode_error;
      if (decode_instruction(current_step_.address, step_info, step_len, decode_error)) {
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

    ReplayUpdate update{};
    update.status = "Step over back";
    fill_update(update);
    post_update(std::move(update));
  });
}

void RewindWorker::step_out_backward() {
  enqueue([this]() {
    std::string error;
    if (!ensure_session_ready(error)) {
      post_error(error);
      return;
    }
    if (!ensure_position(error)) {
      post_update_status(error);
      return;
    }
    if (!ensure_instruction_position(error)) {
      post_error(error);
      return;
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
        post_error(error);
        return;
      }
      if (++guard > kStepGuardLimit) {
        post_error("step out back exceeded step limit");
        return;
      }

      BinaryNinja::InstructionInfo step_info{};
      size_t step_len = 0;
      std::string decode_error;
      if (decode_instruction(current_step_.address, step_info, step_len, decode_error)) {
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

    ReplayUpdate update{};
    update.status = "Step out back";
    fill_update(update);
    post_update(std::move(update));
  });
}

void RewindWorker::run_to_start() {
  enqueue([this]() {
    std::string error;
    if (!ensure_session_ready(error)) {
      post_error(error);
      return;
    }
    if (!move_to_sequence(current_thread_, 0, error)) {
      post_error(error);
      return;
    }
    ReplayUpdate update{};
    update.status = "Rewound to start";
    fill_update(update);
    post_update(std::move(update));
  });
}

void RewindWorker::pause() {
  cancel_requested_.store(true);
  post_status("Paused", controls_enabled_);
}

void RewindWorker::set_gradient_size(size_t size) {
  enqueue([this, size]() {
    size_t clamped = size;
    if (clamped < 1) {
      clamped = 1;
    } else if (clamped > 64) {
      clamped = 64;
    }
    gradient_size_ = clamped;
  });
}

void RewindWorker::run_forward() {
  enqueue([this]() { run_flow_impl(true); });
}

void RewindWorker::run_backward() {
  enqueue([this]() { run_flow_impl(false); });
}

void RewindWorker::run_to_address(uint64_t trace_address, bool forward) {
  enqueue([this, trace_address, forward]() { run_to_address_impl(trace_address, forward); });
}

void RewindWorker::run_to_view_address(uint64_t view_address, bool forward) {
  enqueue([this, view_address, forward]() {
    if (!mapper_.has_primary_mapping()) {
      post_error("address mapper unavailable");
      return;
    }
    auto trace_addr = mapper_.view_to_trace(view_address, 1);
    if (!trace_addr.has_value()) {
      post_error("address not mapped to trace");
      return;
    }
    run_to_address_impl(*trace_addr, forward);
  });
}

void RewindWorker::run_to_address_impl(uint64_t trace_address, bool forward) {
  std::string error;
  if (!ensure_session_ready(error)) {
    post_error(error);
    return;
  }
  if (!ensure_position(error)) {
    post_update_status(error);
    return;
  }
  if (!fast_cursor_) {
    post_error("Flow cursor unavailable");
    return;
  }

  if (current_step_.address == trace_address) {
    ReplayUpdate update{};
    update.status = "At target";
    fill_update(update);
    post_update(std::move(update));
    return;
  }

  std::unordered_set<uint64_t> targets;
  targets.insert(trace_address);

  if (!current_step_.is_block) {
    std::string hit_error;
    auto immediate = find_breakpoint_in_current_block(targets, forward, hit_error);
    if (immediate.has_value()) {
      if (!seek_to_address(*immediate, forward, error)) {
        post_error(error);
        return;
      }
      ReplayUpdate update{};
      update.status = "Reached target";
      fill_update(update);
      post_update(std::move(update));
      return;
    }
  }

  if (!fast_cursor_->seek(current_thread_, current_step_.sequence)) {
    post_error(fast_cursor_->error());
    return;
  }

  w1::rewind::flow_step step{};
  if (!fast_cursor_->step_forward(step)) {
    auto kind = fast_cursor_->error_kind();
    if (forward && kind == w1::rewind::replay_flow_error_kind::end_of_trace) {
      post_update_status("End of trace");
    } else if (!forward && kind == w1::rewind::replay_flow_error_kind::begin_of_trace) {
      post_update_status("Start of trace");
    } else {
      post_error(fast_cursor_->error());
    }
    return;
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
      if (forward && kind == w1::rewind::replay_flow_error_kind::end_of_trace) {
        stop_reason = "End of trace";
      } else if (!forward && kind == w1::rewind::replay_flow_error_kind::begin_of_trace) {
        stop_reason = "Start of trace";
      } else {
        stop_reason = fast_cursor_->error();
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
      post_error(error);
      return;
    }
    if (hit_address.has_value() && current_step_.address != *hit_address) {
      if (!seek_to_address(*hit_address, true, error)) {
        post_error(error);
        return;
      }
    }
  }

  ReplayUpdate update{};
  update.status = stop_reason.empty() ? "Stopped" : stop_reason;
  fill_update(update);
  post_update(std::move(update));
}

} // namespace binja_rewind
