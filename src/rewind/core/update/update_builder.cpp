#include "rewind/core/update/update_builder.hpp"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <sstream>

#include "w1rewind/replay/replay_state_applier.hpp"
#include "w1rewind/replay/stateful_flow_cursor.hpp"
#include "w1rewind/trace/trace_reader.hpp"

namespace binja::rewind::core::update {

void UpdateBuilder::fill_registers(const UpdateContext& ctx, model::ReplayUpdate& update) const {
  if (!ctx.session || !ctx.has_position) {
    return;
  }

  const auto& specs = ctx.session->register_specs();
  const auto& names = ctx.session->register_names();
  if (specs.empty() || names.empty()) {
    return;
  }

  auto values = ctx.session->read_registers();
  size_t count = std::min({specs.size(), names.size(), values.size()});
  update.registers.clear();
  update.registers.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    model::RegisterValue entry{};
    entry.name = names[i];
    if (values[i].has_value()) {
      entry.known = true;
      unsigned width = specs[i].bits ? static_cast<unsigned>((specs[i].bits + 3) / 4) : 16;
      if (width == 0) {
        width = 1;
      }
      std::ostringstream oss;
      oss << "0x" << std::hex << std::setw(width) << std::setfill('0') << *values[i];
      entry.value = oss.str();
    } else {
      entry.known = false;
      entry.value = "??";
    }
    update.registers.push_back(std::move(entry));
  }
}

bool UpdateBuilder::sample_gradient(const UpdateContext& ctx, GradientSample& sample) const {
  if (!ctx.session || !ctx.has_position || !ctx.current_step || !ctx.mapper || !ctx.block_decoder || !ctx.trace_path) {
    return false;
  }
  if (!ctx.trace_index) {
    return false;
  }

  sample.past.clear();
  sample.future.clear();
  sample.current = ctx.current_step->address;

  auto stream = std::make_shared<w1::rewind::trace_reader>(*ctx.trace_path);

  w1::rewind::flow_cursor_config cfg{};
  cfg.stream = stream;
  cfg.index = ctx.trace_index;
  cfg.history_size = static_cast<uint32_t>(ctx.gradient_size + 1);
  cfg.context = &ctx.session->context();

  w1::rewind::flow_cursor cursor(cfg);
  if (!cursor.open()) {
    return false;
  }

  w1::rewind::replay_state state;
  w1::rewind::replay_state_applier applier(ctx.session->context());
  w1::rewind::stateful_flow_cursor stateful(cursor, applier, state);
  stateful.configure(ctx.session->context(), false, false);

  if (!cursor.seek(ctx.current_thread, ctx.current_step->sequence)) {
    return false;
  }

  w1::rewind::flow_step flow{};
  if (!cursor.step_forward(flow)) {
    return false;
  }

  w1::rewind::replay_instruction_cursor inst(stateful);
  inst.set_decoder(ctx.block_decoder);
  inst.set_position(*ctx.current_step);
  sample.current = inst.current_step().address;

  w1::rewind::flow_step step = inst.current_step();
  for (size_t i = 0; i < ctx.gradient_size; ++i) {
    if (!inst.step_backward(step)) {
      break;
    }
    sample.past.push_back(step.address);
  }

  stateful.configure(ctx.session->context(), false, false);
  if (!cursor.seek(ctx.current_thread, ctx.current_step->sequence)) {
    return true;
  }
  if (!cursor.step_forward(flow)) {
    return true;
  }
  w1::rewind::replay_instruction_cursor inst_fwd(stateful);
  inst_fwd.set_decoder(ctx.block_decoder);
  inst_fwd.set_position(*ctx.current_step);
  step = inst_fwd.current_step();
  for (size_t i = 0; i < ctx.gradient_size; ++i) {
    if (!inst_fwd.step_forward(step)) {
      break;
    }
    sample.future.push_back(step.address);
  }

  return true;
}

void UpdateBuilder::fill_update(const UpdateContext& ctx, model::ReplayUpdate& update) const {
  if (ctx.trace_path) {
    update.trace_path = *ctx.trace_path;
  }
  update.trace_loaded = ctx.trace_loaded;
  update.controls_enabled = ctx.controls_enabled;

  if (ctx.has_position && ctx.current_step && ctx.mapper) {
    update.has_position = true;
    update.thread_id = ctx.current_thread;
    update.sequence = ctx.current_step->sequence;

    GradientSample sample{};
    if (sample_gradient(ctx, sample)) {
      update.trace_address = sample.current;
      update.view_address = ctx.mapper->trace_to_view(sample.current, 1);
      update.past_addresses.clear();
      update.future_addresses.clear();
      for (auto addr : sample.past) {
        if (auto mapped = ctx.mapper->trace_to_view(addr, 1)) {
          update.past_addresses.push_back(*mapped);
        }
      }
      for (auto addr : sample.future) {
        if (auto mapped = ctx.mapper->trace_to_view(addr, 1)) {
          update.future_addresses.push_back(*mapped);
        }
      }
    } else {
      update.trace_address = ctx.current_step->address;
      update.view_address = ctx.mapper->trace_to_view(ctx.current_step->address, 1);
    }
  }

  fill_registers(ctx, update);
}

} // namespace binja::rewind::core::update
