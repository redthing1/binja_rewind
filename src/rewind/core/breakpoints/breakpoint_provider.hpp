#pragma once

#include <string>
#include <unordered_set>

#include "binaryninjaapi.h"
#include "rewind/core/mapping/address_mapper.hpp"

namespace binja::rewind::core::breakpoints {

class BreakpointProvider {
public:
  std::unordered_set<uint64_t> collect_breakpoints(
      const BinaryNinja::Ref<BinaryNinja::BinaryView>& view, const mapping::AddressMapper& mapper
  ) const;
};

} // namespace binja::rewind::core::breakpoints
