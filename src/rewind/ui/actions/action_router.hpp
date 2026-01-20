#pragma once

#include "uicontext.h"

namespace BinaryNinja {
class BinaryView;
}

namespace binja::rewind::ui {

bool can_dispatch_action_for_view(const QString& name, BinaryNinja::BinaryView* view);
bool dispatch_action_for_view(const QString& name, BinaryNinja::BinaryView* view);

class RewindActionRouter final : public UIContextNotification {
public:
  static void init();
  static void refresh_bindings();

  void OnActionExecuted(
      UIContext* context, UIActionHandler* handler, const QString& name, const UIActionContext& ctx,
      std::function<void(const UIActionContext&)>& action
  ) override;
};

} // namespace binja::rewind::ui
