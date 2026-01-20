#pragma once

#include <cstddef>

#include "binaryninjaapi.h"
#include "uitypes.h"

namespace binja::rewind::ui {

void register_settings();
size_t get_frontier_size(const BinaryViewRef& view);

} // namespace binja::rewind::ui
