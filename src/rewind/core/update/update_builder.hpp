#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rewind/core/decode/bn_block_decoder.hpp"
#include "rewind/core/mapping/address_mapper.hpp"
#include "rewind/core/model/replay_types.hpp"
#include "w1rewind/replay/flow_cursor.hpp"
#include "w1rewind/replay/replay_instruction_cursor.hpp"
#include "w1rewind/replay/replay_session.hpp"
#include "w1rewind/trace/trace_index.hpp"

namespace binja::rewind::core::update {

struct GradientSample {
  uint64_t current = 0;
  std::vector<uint64_t> past;
  std::vector<uint64_t> future;
};

struct UpdateContext {
  const w1::rewind::replay_session* session = nullptr;
  const mapping::AddressMapper* mapper = nullptr;
  decode::BnBlockDecoder* block_decoder = nullptr;
  const std::string* trace_path = nullptr;
  std::shared_ptr<w1::rewind::trace_index> trace_index;
  bool trace_loaded = false;
  bool controls_enabled = false;
  bool has_position = false;
  uint64_t current_thread = 0;
  const w1::rewind::flow_step* current_step = nullptr;
  size_t gradient_size = 8;
};

class UpdateBuilder {
public:
  void fill_update(const UpdateContext& ctx, model::ReplayUpdate& update) const;

private:
  void fill_registers(const UpdateContext& ctx, model::ReplayUpdate& update) const;
  bool sample_gradient(const UpdateContext& ctx, GradientSample& sample) const;
};

} // namespace binja::rewind::core::update
