#include "rewind/core/breakpoints/breakpoint_provider.hpp"

namespace binja::rewind::core::breakpoints {

std::unordered_set<uint64_t> BreakpointProvider::collect_breakpoints(
    const BinaryNinja::Ref<BinaryNinja::BinaryView>& view, const mapping::AddressMapper& mapper
) const {
  std::unordered_set<uint64_t> result;
  if (!view) {
    return result;
  }

  auto metadata = view->QueryMetadata("debugger.breakpoints");
  if (!metadata || !metadata->IsArray()) {
    return result;
  }

  for (const auto& entry : metadata->GetArray()) {
    if (!entry || !entry->IsKeyValueStore()) {
      continue;
    }

    auto kv = entry->GetKeyValueStore();
    auto module = kv.find("module");
    auto offset = kv.find("offset");
    if (module == kv.end() || offset == kv.end()) {
      continue;
    }
    if (!module->second || !module->second->IsString()) {
      continue;
    }
    if (!offset->second || !offset->second->IsUnsignedInteger()) {
      continue;
    }
    auto enabled = kv.find("enabled");
    if (enabled != kv.end() && enabled->second && enabled->second->IsBoolean() && !enabled->second->GetBoolean()) {
      continue;
    }

    const std::string module_name = module->second->GetString();
    const uint64_t offset_value = offset->second->GetUnsignedInteger();
    auto trace_addr = mapper.module_offset_to_trace(module_name, offset_value);
    if (trace_addr.has_value()) {
      result.insert(*trace_addr);
    }
  }

  return result;
}

} // namespace binja::rewind::core::breakpoints
