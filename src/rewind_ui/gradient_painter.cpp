#include "rewind_ui/gradient_painter.hpp"

#include <algorithm>

#include "binaryninjaapi.h"

namespace {

std::pair<uint8_t, uint8_t> compute_fade(
    size_t index, size_t total, uint8_t rgb_start, uint8_t rgb_end, uint8_t alpha_start, uint8_t alpha_end
) {
  if (total <= 1) {
    return {rgb_start, alpha_start};
  }
  double ratio = static_cast<double>(index) / static_cast<double>(total - 1);
  auto rgb = static_cast<uint8_t>(rgb_start + (rgb_end - rgb_start) * ratio);
  auto alpha = static_cast<uint8_t>(alpha_start + (alpha_end - alpha_start) * ratio);
  return {rgb, alpha};
}

} // namespace

void GradientPainter::clear(const BinaryViewRef& view) {
  if (!view) {
    highlighted_.clear();
    return;
  }
  for (auto address : highlighted_) {
    clear_address(view, address);
  }
  highlighted_.clear();
}

void GradientPainter::paint(
    const BinaryViewRef& view, const std::optional<uint64_t>& current, const std::vector<uint64_t>& past,
    const std::vector<uint64_t>& future
) {
  if (!view) {
    return;
  }

  clear(view);

  if (current.has_value()) {
    highlight_address(view, *current, 255, 0, 255, 255);
  }

  size_t past_count = std::min(frontier_size_, past.size());
  for (size_t i = 0; i < past_count; ++i) {
    auto [rgb, alpha] = compute_fade(i, past_count, 200, 80, 200, 50);
    highlight_address(view, past[i], rgb, 0, 0, alpha);
  }

  size_t future_count = std::min(frontier_size_, future.size());
  for (size_t i = 0; i < future_count; ++i) {
    auto [rgb, alpha] = compute_fade(i, future_count, 200, 80, 200, 50);
    highlight_address(view, future[i], 0, 0, rgb, alpha);
  }
}

void GradientPainter::highlight_address(
    const BinaryViewRef& view, uint64_t address, uint8_t r, uint8_t g, uint8_t b, uint8_t a
) {
  if (!view) {
    return;
  }
  auto arch = view->GetDefaultArchitecture();
  auto funcs = view->GetAnalysisFunctionsContainingAddress(address);
  for (const auto& func : funcs) {
    func->SetAutoInstructionHighlight(arch, address, r, g, b, a);
  }
  if (!funcs.empty()) {
    highlighted_.insert(address);
  }
}

void GradientPainter::clear_address(const BinaryViewRef& view, uint64_t address) {
  if (!view) {
    return;
  }
  auto arch = view->GetDefaultArchitecture();
  auto funcs = view->GetAnalysisFunctionsContainingAddress(address);
  for (const auto& func : funcs) {
    func->SetAutoInstructionHighlight(arch, address, NoHighlightColor, 0);
  }
}
