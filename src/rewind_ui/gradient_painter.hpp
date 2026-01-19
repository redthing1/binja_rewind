#pragma once

#include <optional>
#include <unordered_set>
#include <vector>

#include "binaryninjaapi.h"
#include "uitypes.h"

class GradientPainter {
public:
  explicit GradientPainter(size_t frontier_size = 8) : frontier_size_(frontier_size) {}

  void set_frontier_size(size_t frontier) { frontier_size_ = frontier; }
  void clear(const BinaryViewRef& view);
  void paint(
      const BinaryViewRef& view, const std::optional<uint64_t>& current, const std::vector<uint64_t>& past,
      const std::vector<uint64_t>& future
  );

private:
  void highlight_address(const BinaryViewRef& view, uint64_t address, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
  void clear_address(const BinaryViewRef& view, uint64_t address);

  size_t frontier_size_ = 8;
  std::unordered_set<uint64_t> highlighted_;
};
