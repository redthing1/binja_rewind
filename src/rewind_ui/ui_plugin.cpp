#include "binaryninjaapi.h"
#include "rewind_ui/sidebar_widget.hpp"
#include "sidebar.h"

using namespace BinaryNinja;

extern "C" {

BN_DECLARE_CORE_ABI_VERSION
BN_DECLARE_UI_ABI_VERSION

BINARYNINJAPLUGIN bool UIPluginInit() {
  Sidebar::addSidebarWidgetType(new RewindSidebarWidgetType());
  return true;
}
}
