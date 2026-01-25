#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "binaryninjaapi.h"
#include "rewind/core/mapping/address_mapper.hpp"

namespace binja::rewind::core::decode {

class InstructionDecoder {
public:
  struct instruction_mode {
    bool mode_valid;
    bool thumb;

    constexpr instruction_mode(bool mode_valid_value = false, bool thumb_value = false)
        : mode_valid(mode_valid_value), thumb(thumb_value) {}
  };

  struct instruction_semantics {
    size_t length = 0;
    bool is_call = false;
    bool is_return = false;
    bool is_syscall = false;
  };

  struct instruction_detail {
    instruction_semantics semantics;
    BinaryNinja::InstructionInfo info;
  };

  explicit InstructionDecoder(
      BinaryNinja::Ref<BinaryNinja::BinaryView> view = nullptr, const mapping::AddressMapper* mapper = nullptr
  )
      : view_(std::move(view)), mapper_(mapper) {}

  void set_view(BinaryNinja::Ref<BinaryNinja::BinaryView> view) { view_ = std::move(view); }
  void set_mapper(const mapping::AddressMapper* mapper) { mapper_ = mapper; }

  bool decode_instruction(
      uint64_t trace_address, BinaryNinja::InstructionInfo& info, size_t& length, std::string& error,
      instruction_mode mode = {}
  ) const;
  bool decode_instruction_semantics(
      uint64_t trace_address, instruction_semantics& semantics, std::string& error, instruction_mode mode = {}
  ) const;
  bool decode_instruction_detail(
      uint64_t trace_address, instruction_detail& detail, std::string& error, instruction_mode mode = {}
  ) const;

private:
  BinaryNinja::Ref<BinaryNinja::BinaryView> view_;
  const mapping::AddressMapper* mapper_ = nullptr;
};

} // namespace binja::rewind::core::decode
