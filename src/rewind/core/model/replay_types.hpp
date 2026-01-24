#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace binja::rewind::core::model {

struct ThreadInfo {
  uint64_t id = 0;
  std::string name;
  bool started = false;
  bool ended = false;
};

struct RegisterValue {
  std::string name;
  std::string value;
  bool known = false;
};

struct TraceSummary {
  std::string arch;
  std::string os;
  std::string abi;
  std::string cpu;
  uint16_t trace_version = 0;
  bool has_blocks = false;
  bool has_registers = false;
  bool has_memory_access = false;
  bool has_memory_values = false;
  bool has_stack_snapshot = false;
  uint64_t thread_count = 0;
  uint64_t module_count = 0;
};

struct TraceModule {
  std::string path;
  uint64_t base = 0;
  uint64_t size = 0;
  uint32_t permissions = 0;
};

struct ReplayUpdate {
  std::string status;
  std::string trace_path;
  bool trace_loaded = false;
  bool trace_cleared = false;
  bool controls_enabled = false;
  bool status_only = false;

  bool has_position = false;
  uint64_t thread_id = 0;
  uint64_t sequence = 0;
  uint64_t trace_address = 0;
  std::optional<uint64_t> view_address;

  std::vector<ThreadInfo> threads;
  std::vector<RegisterValue> registers;
  std::vector<uint64_t> past_addresses;
  std::vector<uint64_t> future_addresses;

  bool trace_info_changed = false;
  TraceSummary summary;
  std::vector<TraceModule> modules;
};

} // namespace binja::rewind::core::model
