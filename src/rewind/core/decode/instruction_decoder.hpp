#pragma once

#include <cstdint>
#include <string>

#include "binaryninjaapi.h"
#include "rewind/core/mapping/address_mapper.hpp"

namespace binja::rewind::core::decode {

class InstructionDecoder {
public:
  explicit InstructionDecoder(
      BinaryNinja::Ref<BinaryNinja::BinaryView> view = nullptr, const mapping::AddressMapper* mapper = nullptr
  )
      : view_(std::move(view)), mapper_(mapper) {}

  void set_view(BinaryNinja::Ref<BinaryNinja::BinaryView> view) { view_ = std::move(view); }
  void set_mapper(const mapping::AddressMapper* mapper) { mapper_ = mapper; }

  bool decode_instruction(
      uint64_t trace_address, BinaryNinja::InstructionInfo& info, size_t& length, std::string& error
  ) const;

private:
  BinaryNinja::Ref<BinaryNinja::BinaryView> view_;
  const mapping::AddressMapper* mapper_ = nullptr;
};

} // namespace binja::rewind::core::decode
