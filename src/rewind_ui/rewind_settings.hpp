#pragma once

#include <cstddef>

#include "binaryninjaapi.h"
#include "uitypes.h"

namespace rewind_ui {

void register_settings();
size_t get_frontier_size(const BinaryViewRef& view);

} // namespace rewind_ui
