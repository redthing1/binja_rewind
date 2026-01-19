#pragma once

#include <string>
#include <unordered_set>

#include "binaryninjaapi.h"
#include "rewind_core/address_mapper.hpp"

namespace binja_rewind {

class BreakpointProvider {
public:
  std::unordered_set<uint64_t> collect_breakpoints(
      const BinaryNinja::Ref<BinaryNinja::BinaryView>& view, const AddressMapper& mapper
  ) const;
};

} // namespace binja_rewind
