#include "rewind_core/address_mapper.hpp"

#include <algorithm>

namespace binja_rewind {

std::string AddressMapper::basename(std::string_view path) {
  size_t end = path.size();
  while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\')) {
    --end;
  }
  size_t start = end;
  while (start > 0) {
    char c = path[start - 1];
    if (c == '/' || c == '\\') {
      break;
    }
    --start;
  }
  return std::string(path.substr(start, end - start));
}

bool AddressMapper::in_range(uint64_t base, uint64_t size, uint64_t address, uint64_t length) {
  if (length == 0) {
    return false;
  }
  if (size == 0) {
    return true;
  }
  if (address < base) {
    return false;
  }
  uint64_t offset = address - base;
  if (offset > size - length) {
    return false;
  }
  return true;
}

void AddressMapper::reset() {
  primary_.reset();
  modules_.clear();
}

bool AddressMapper::configure(
    const BinaryNinja::Ref<BinaryNinja::BinaryView>& view, const w1::rewind::replay_context& context, std::string* error
) {
  reset();

  auto logger = logger_;
  if (!logger) {
    logger = BinaryNinja::LogRegistry::CreateLogger("Rewind");
  }

  if (!view) {
    if (error) {
      *error = "binary view unavailable";
    }
    return false;
  }

  modules_.reserve(context.modules.size());
  for (const auto& module : context.modules) {
    ModuleInfo info{};
    info.base = module.base;
    info.size = module.size;
    info.path = module.path;
    info.basename = basename(module.path);
    modules_.push_back(std::move(info));
  }

  std::string view_path;
  std::string view_original;
  if (auto file = view->GetFile()) {
    view_path = file->GetFilename();
    view_original = file->GetOriginalFilename();
  }

  logger->LogInfo(
      "Rewind: view path='%s' original='%s' image_base=0x%llx start=0x%llx end=0x%llx length=0x%llx", view_path.c_str(),
      view_original.c_str(), static_cast<unsigned long long>(view->GetImageBase()),
      static_cast<unsigned long long>(view->GetStart()), static_cast<unsigned long long>(view->GetEnd()),
      static_cast<unsigned long long>(view->GetLength())
  );

  if (modules_.empty()) {
    logger->LogWarn("Rewind: trace has no module entries to map against");
  } else {
    for (const auto& module : modules_) {
      logger->LogInfo(
          "Rewind: trace module '%s' base=0x%llx size=0x%llx", module.path.c_str(),
          static_cast<unsigned long long>(module.base), static_cast<unsigned long long>(module.size)
      );
    }
  }

  const std::string view_base = basename(view_path);
  const std::string view_original_base = basename(view_original);

  auto score_module = [&](const ModuleInfo& module) -> int {
    int score = 0;
    if (!view_path.empty() && module.path == view_path) {
      score = 4;
    }
    if (!view_original.empty() && module.path == view_original) {
      score = std::max(score, 4);
    }
    if (!view_base.empty() && module.basename == view_base) {
      score = std::max(score, 2);
    }
    if (!view_original_base.empty() && module.basename == view_original_base) {
      score = std::max(score, 2);
    }
    return score;
  };

  int best_score = 0;
  const ModuleInfo* best_module = nullptr;
  for (const auto& module : modules_) {
    int score = score_module(module);
    if (score > best_score) {
      best_score = score;
      best_module = &module;
    } else if (score == best_score && score > 0 && best_module) {
      if (module.size > best_module->size) {
        best_module = &module;
      }
    }
  }

  if (!best_module) {
    if (error) {
      *error = "no module in trace matched the binary view";
    }
    logger->LogError(
        "Rewind: no module matched view (view path='%s', original='%s')", view_path.c_str(), view_original.c_str()
    );
    return false;
  }

  ModuleMapping mapping{};
  mapping.trace_base = best_module->base;
  mapping.trace_size = best_module->size;
  mapping.view_base = view->GetImageBase();
  mapping.view_size = view->GetLength();
  mapping.trace_path = best_module->path;
  mapping.view_path = view_path;
  primary_ = mapping;
  logger->LogInfo(
      "Rewind: mapped module '%s' (trace base=0x%llx) to view base=0x%llx", best_module->path.c_str(),
      static_cast<unsigned long long>(mapping.trace_base), static_cast<unsigned long long>(mapping.view_base)
  );
  return true;
}

std::optional<uint64_t> AddressMapper::trace_to_view(uint64_t trace_address, uint64_t size) const {
  if (!primary_.has_value()) {
    return std::nullopt;
  }
  const auto& mapping = *primary_;
  if (!in_range(mapping.trace_base, mapping.trace_size, trace_address, size)) {
    return std::nullopt;
  }
  return mapping.view_base + (trace_address - mapping.trace_base);
}

std::optional<uint64_t> AddressMapper::view_to_trace(uint64_t view_address, uint64_t size) const {
  if (!primary_.has_value()) {
    return std::nullopt;
  }
  const auto& mapping = *primary_;
  uint64_t view_size = mapping.view_size != 0 ? mapping.view_size : mapping.trace_size;
  if (!in_range(mapping.view_base, view_size, view_address, size)) {
    return std::nullopt;
  }
  return mapping.trace_base + (view_address - mapping.view_base);
}

std::optional<uint64_t> AddressMapper::module_offset_to_trace(const std::string& module_name, uint64_t offset) const {
  if (modules_.empty()) {
    return std::nullopt;
  }

  const std::string name_base = basename(module_name);
  const ModuleInfo* best = nullptr;
  int best_score = 0;
  for (const auto& module : modules_) {
    int score = 0;
    if (!module_name.empty() && module.path == module_name) {
      score = 4;
    } else if (!name_base.empty() && module.basename == name_base) {
      score = 2;
    }
    if (score > best_score) {
      best_score = score;
      best = &module;
    } else if (score == best_score && score > 0 && best && module.size > best->size) {
      best = &module;
    }
  }

  if (!best) {
    return std::nullopt;
  }
  if (best->size != 0 && offset >= best->size) {
    return std::nullopt;
  }
  return best->base + offset;
}

} // namespace binja_rewind
