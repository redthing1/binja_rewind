#pragma once

#include <string>

#include "binaryninjaapi.h"
#include "rewind/core/mapping/address_mapper.hpp"
#include "w1rewind/replay/replay_decode.hpp"

namespace binja::rewind::core::decode {

class BnBlockDecoder : public w1::rewind::replay_block_decoder {
public:
  explicit BnBlockDecoder(
      BinaryNinja::Ref<BinaryNinja::BinaryView> view = nullptr, const mapping::AddressMapper* mapper = nullptr
  )
      : view_(std::move(view)), mapper_(mapper) {}

  void set_view(BinaryNinja::Ref<BinaryNinja::BinaryView> view) { view_ = std::move(view); }
  void set_mapper(const mapping::AddressMapper* mapper) { mapper_ = mapper; }

  bool decode_block(
      const w1::rewind::replay_context& context, const w1::rewind::flow_step& flow,
      w1::rewind::replay_decoded_block& out, std::string& error
  ) override;

private:
  BinaryNinja::Ref<BinaryNinja::BinaryView> view_;
  const mapping::AddressMapper* mapper_ = nullptr;
};

} // namespace binja::rewind::core::decode
