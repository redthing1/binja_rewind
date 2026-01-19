#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "binaryninjaapi.h"
#include "w1rewind/replay/replay_context.hpp"

namespace binja_rewind {

struct ModuleMapping {
  uint64_t trace_base = 0;
  uint64_t trace_size = 0;
  uint64_t view_base = 0;
  uint64_t view_size = 0;
  std::string trace_path;
  std::string view_path;
};

class AddressMapper {
public:
  void reset();
  bool configure(
      const BinaryNinja::Ref<BinaryNinja::BinaryView>& view, const w1::rewind::replay_context& context,
      std::string* error
  );
  void set_logger(const BinaryNinja::Ref<BinaryNinja::Logger>& logger) { logger_ = logger; }

  bool has_primary_mapping() const { return primary_.has_value(); }
  std::optional<uint64_t> trace_to_view(uint64_t trace_address, uint64_t size = 1) const;
  std::optional<uint64_t> view_to_trace(uint64_t view_address, uint64_t size = 1) const;
  std::optional<uint64_t> module_offset_to_trace(const std::string& module_name, uint64_t offset) const;

private:
  struct ModuleInfo {
    uint64_t base = 0;
    uint64_t size = 0;
    std::string path;
    std::string basename;
  };

  static std::string basename(std::string_view path);
  static bool in_range(uint64_t base, uint64_t size, uint64_t address, uint64_t length);

  std::optional<ModuleMapping> primary_;
  std::vector<ModuleInfo> modules_;
  BinaryNinja::Ref<BinaryNinja::Logger> logger_;
};

} // namespace binja_rewind
