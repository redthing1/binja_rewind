#include "binaryninjaapi.h"
#include "rewind_ui/action_router.hpp"
#include "rewind_ui/rewind_actions.hpp"
#include "rewind_ui/rewind_settings.hpp"
#include "rewind_ui/sidebar_widget.hpp"
#include "sidebar.h"

using namespace BinaryNinja;

extern "C" {

BN_DECLARE_CORE_ABI_VERSION
BN_DECLARE_UI_ABI_VERSION

BINARYNINJAPLUGIN bool UIPluginInit() {
  rewind_ui::register_settings();
  rewind_ui::RewindActionRouter::init();
  PluginCommand::Register(
      rewind_ui::kDefineFunctionsFromTraceCommand, "Define functions for executed trace code",
      [](BinaryView* view) {
        rewind_ui::dispatch_action_for_view(rewind_ui::action_name(rewind_ui::ActionId::DefineFunctionsFromTrace), view);
      },
      [](BinaryView* view) {
        return rewind_ui::can_dispatch_action_for_view(
            rewind_ui::action_name(rewind_ui::ActionId::DefineFunctionsFromTrace), view
        );
      }
  );
  Sidebar::addSidebarWidgetType(new RewindSidebarWidgetType());
  return true;
}
}
