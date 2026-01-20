#include "rewind/core/decode/bn_block_decoder.hpp"

#include <vector>

#include "w1rewind/replay/replay_flow_cursor.hpp"

namespace binja::rewind::core::decode {

bool BnBlockDecoder::decode_block(
    const w1::rewind::replay_context& context, const w1::rewind::flow_step& flow, w1::rewind::replay_decoded_block& out,
    std::string& error
) {
  error.clear();
  (void) context;

  if (!view_) {
    error = "binary view unavailable";
    return false;
  }
  if (!mapper_) {
    error = "address mapper unavailable";
    return false;
  }
  if (flow.size == 0) {
    error = "block size is zero";
    return false;
  }

  auto view_addr = mapper_->trace_to_view(flow.address, flow.size);
  if (!view_addr.has_value()) {
    error = "block address not mapped to binary view";
    return false;
  }

  auto arch = view_->GetDefaultArchitecture();
  if (!arch) {
    error = "binary view architecture unavailable";
    return false;
  }

  std::vector<uint8_t> bytes(flow.size);
  size_t read = view_->Read(bytes.data(), *view_addr, bytes.size());
  if (read < bytes.size()) {
    error = "failed to read block bytes from binary view";
    return false;
  }

  out.address = flow.address;
  out.size = flow.size;
  out.instructions.clear();

  size_t offset = 0;
  while (offset < bytes.size()) {
    BinaryNinja::InstructionInfo info;
    size_t max_len = bytes.size() - offset;
    if (!arch->GetInstructionInfo(bytes.data() + offset, *view_addr + offset, max_len, info)) {
      error = "failed to decode instruction info";
      return false;
    }
    if (info.length == 0 || info.length > max_len) {
      error = "invalid instruction length";
      return false;
    }

    w1::rewind::replay_decoded_instruction inst{};
    inst.offset = static_cast<uint32_t>(offset);
    inst.size = static_cast<uint32_t>(info.length);
    inst.bytes.assign(bytes.begin() + offset, bytes.begin() + offset + info.length);
    out.instructions.push_back(std::move(inst));
    offset += info.length;
  }

  return true;
}

} // namespace binja::rewind::core::decode
