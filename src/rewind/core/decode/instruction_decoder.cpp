#include "rewind/core/decode/instruction_decoder.hpp"

#include <vector>

#include "rewind/core/decode/instruction_classifier.hpp"

namespace binja::rewind::core::decode {

namespace {

struct decode_buffer {
  BinaryNinja::Ref<BinaryNinja::Architecture> arch;
  uint64_t view_address = 0;
  std::vector<uint8_t> bytes;
};

bool fetch_bytes(
    const BinaryNinja::Ref<BinaryNinja::BinaryView>& view, const mapping::AddressMapper* mapper, uint64_t trace_address,
    decode_buffer& out, std::string& error
) {
  error.clear();
  out = decode_buffer{};

  if (!view) {
    error = "binary view unavailable";
    return false;
  }
  if (!mapper || !mapper->has_primary_mapping()) {
    error = "address mapper unavailable";
    return false;
  }

  auto view_addr = mapper->trace_to_view(trace_address, 1);
  if (!view_addr.has_value()) {
    error = "address not mapped to binary view";
    return false;
  }

  auto arch = view->GetDefaultArchitecture();
  if (!arch) {
    error = "binary view architecture unavailable";
    return false;
  }

  size_t max_len = arch->GetMaxInstructionLength();
  if (max_len == 0) {
    max_len = 16;
  }

  out.bytes.resize(max_len);
  size_t read = view->Read(out.bytes.data(), *view_addr, out.bytes.size());
  if (read == 0) {
    error = "failed to read instruction bytes";
    return false;
  }
  out.bytes.resize(read);
  out.view_address = *view_addr;

  uint64_t decode_addr = out.view_address;
  if (auto associated = arch->GetAssociatedArchitectureByAddress(decode_addr); associated) {
    arch = associated;
  }
  out.arch = arch;
  return true;
}

bool extract_mnemonic(const std::vector<BinaryNinja::InstructionTextToken>& tokens, std::string& mnemonic) {
  for (const auto& token : tokens) {
    if (token.type == InstructionToken) {
      mnemonic = token.text;
      return !mnemonic.empty();
    }
  }
  return false;
}

} // namespace

bool InstructionDecoder::decode_instruction(
    uint64_t trace_address, BinaryNinja::InstructionInfo& info, size_t& length, std::string& error
) const {
  decode_buffer buffer;
  if (!fetch_bytes(view_, mapper_, trace_address, buffer, error)) {
    length = 0;
    return false;
  }

  BinaryNinja::InstructionInfo inst_info;
  if (!buffer.arch->GetInstructionInfo(buffer.bytes.data(), buffer.view_address, buffer.bytes.size(), inst_info)) {
    error = "failed to decode instruction info";
    return false;
  }
  if (inst_info.length == 0 || inst_info.length > buffer.bytes.size()) {
    error = "invalid instruction length";
    return false;
  }

  info = inst_info;
  length = inst_info.length;
  return true;
}

bool InstructionDecoder::decode_instruction_semantics(
    uint64_t trace_address, instruction_semantics& semantics, std::string& error
) const {
  semantics = instruction_semantics{};

  decode_buffer buffer;
  if (!fetch_bytes(view_, mapper_, trace_address, buffer, error)) {
    return false;
  }

  BinaryNinja::InstructionInfo inst_info;
  if (!buffer.arch->GetInstructionInfo(buffer.bytes.data(), buffer.view_address, buffer.bytes.size(), inst_info)) {
    error = "failed to decode instruction info";
    return false;
  }
  if (inst_info.length == 0 || inst_info.length > buffer.bytes.size()) {
    error = "invalid instruction length";
    return false;
  }

  semantics.length = inst_info.length;
  for (size_t i = 0; i < inst_info.branchCount; ++i) {
    switch (inst_info.branchType[i]) {
    case CallDestination:
      semantics.is_call = true;
      break;
    case FunctionReturn:
      semantics.is_return = true;
      break;
    case SystemCall:
      semantics.is_syscall = true;
      semantics.is_call = true;
      break;
    default:
      break;
    }
  }

  if (semantics.is_call && semantics.is_return) {
    return true;
  }

  size_t text_len = buffer.bytes.size();
  std::vector<BinaryNinja::InstructionTextToken> tokens;
  if (!buffer.arch->GetInstructionText(buffer.bytes.data(), buffer.view_address, text_len, tokens) || tokens.empty()) {
    return true;
  }

  std::string mnemonic;
  if (!extract_mnemonic(tokens, mnemonic)) {
    return true;
  }

  const std::string arch_name = buffer.arch ? buffer.arch->GetName() : std::string();
  if (!semantics.is_call && is_call_mnemonic(mnemonic, arch_name)) {
    semantics.is_call = true;
  }
  if (!semantics.is_return && is_return_mnemonic(mnemonic, arch_name)) {
    semantics.is_return = true;
  }

  return true;
}

} // namespace binja::rewind::core::decode
