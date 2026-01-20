#include "rewind/core/decode/instruction_decoder.hpp"

#include <vector>

namespace binja::rewind::core::decode {

bool InstructionDecoder::decode_instruction(
    uint64_t trace_address, BinaryNinja::InstructionInfo& info, size_t& length, std::string& error
) const {
  error.clear();
  length = 0;

  if (!view_) {
    error = "binary view unavailable";
    return false;
  }
  if (!mapper_) {
    error = "address mapper unavailable";
    return false;
  }
  if (!mapper_->has_primary_mapping()) {
    error = "address mapper unavailable";
    return false;
  }

  auto view_addr = mapper_->trace_to_view(trace_address, 1);
  if (!view_addr.has_value()) {
    error = "address not mapped to binary view";
    return false;
  }

  auto arch = view_->GetDefaultArchitecture();
  if (!arch) {
    error = "binary view architecture unavailable";
    return false;
  }

  size_t max_len = arch->GetMaxInstructionLength();
  if (max_len == 0) {
    max_len = 16;
  }

  std::vector<uint8_t> buffer(max_len);
  size_t read = view_->Read(buffer.data(), *view_addr, buffer.size());
  if (read == 0) {
    error = "failed to read instruction bytes";
    return false;
  }

  BinaryNinja::InstructionInfo inst_info;
  if (!arch->GetInstructionInfo(buffer.data(), *view_addr, read, inst_info)) {
    error = "failed to decode instruction info";
    return false;
  }
  if (inst_info.length == 0 || inst_info.length > read) {
    error = "invalid instruction length";
    return false;
  }

  info = inst_info;
  length = inst_info.length;
  return true;
}

} // namespace binja::rewind::core::decode
