#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "binaryninjaapi.h"
#include "rewind/core/mapping/address_mapper.hpp"
#include "w1rewind/replay/replay_session.hpp"
#include "w1rewind/trace/trace_index.hpp"

namespace binja::rewind::core::functions {

struct DefineFunctionsResult {
  bool ok = false;
  std::string error;
  size_t steps_scanned = 0;
  size_t candidates = 0;
  size_t created = 0;
  size_t skipped = 0;
  size_t no_segment = 0;
};

class TraceFunctionDefiner {
public:
  DefineFunctionsResult define_functions(
      const w1::rewind::replay_session& session, const std::shared_ptr<w1::rewind::trace_index>& index,
      const mapping::AddressMapper& mapper, const BinaryNinja::Ref<BinaryNinja::BinaryView>& view,
      const std::string& trace_path, const BinaryNinja::Ref<BinaryNinja::Logger>& logger
  ) const;
};

} // namespace binja::rewind::core::functions
