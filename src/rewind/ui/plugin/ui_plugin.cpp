#include "action.h"
#include "binaryninjaapi.h"
#include "rewind/ui/actions/action_router.hpp"
#include "rewind/ui/actions/rewind_actions.hpp"
#include "rewind/ui/settings/rewind_settings.hpp"
#include "rewind/ui/widgets/sidebar_widget.hpp"
#include "sidebar.h"

using namespace BinaryNinja;

extern "C" {

BN_DECLARE_CORE_ABI_VERSION
BN_DECLARE_UI_ABI_VERSION

BINARYNINJAPLUGIN bool UIPluginInit() {
  binja::rewind::ui::register_settings();
  binja::rewind::ui::RewindActionRouter::init();
  UIAction::registerAction(binja::rewind::ui::kDefineFunctionsFromTraceSelectionAction);
  PluginCommand::Register(
      binja::rewind::ui::kDefineFunctionsFromTraceCommand, "Define functions for executed trace code",
      [](BinaryView* view) {
        binja::rewind::ui::dispatch_action_for_view(
            binja::rewind::ui::action_name(binja::rewind::ui::ActionId::DefineFunctionsFromTrace), view
        );
      },
      [](BinaryView* view) {
        return binja::rewind::ui::can_dispatch_action_for_view(
            binja::rewind::ui::action_name(binja::rewind::ui::ActionId::DefineFunctionsFromTrace), view
        );
      }
  );
  Sidebar::addSidebarWidgetType(new binja::rewind::ui::RewindSidebarWidgetType());
  return true;
}
}
