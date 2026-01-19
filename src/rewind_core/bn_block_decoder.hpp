#pragma once

#include <string>

#include "binaryninjaapi.h"
#include "rewind_core/address_mapper.hpp"
#include "w1rewind/replay/replay_decode.hpp"

namespace binja_rewind {

class BnBlockDecoder : public w1::rewind::replay_block_decoder {
public:
  explicit BnBlockDecoder(
      BinaryNinja::Ref<BinaryNinja::BinaryView> view = nullptr, const AddressMapper* mapper = nullptr
  )
      : view_(std::move(view)), mapper_(mapper) {}

  void set_view(BinaryNinja::Ref<BinaryNinja::BinaryView> view) { view_ = std::move(view); }
  void set_mapper(const AddressMapper* mapper) { mapper_ = mapper; }

  bool decode_block(
      const w1::rewind::replay_context& context, const w1::rewind::flow_step& flow,
      w1::rewind::replay_decoded_block& out, std::string& error
  ) override;

private:
  BinaryNinja::Ref<BinaryNinja::BinaryView> view_;
  const AddressMapper* mapper_ = nullptr;
};

} // namespace binja_rewind
