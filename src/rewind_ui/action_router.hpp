#pragma once

#include "uicontext.h"

namespace rewind_ui {

class RewindActionRouter final : public UIContextNotification {
public:
  static void init();
  static void refresh_bindings();

  void OnActionExecuted(
      UIContext* context, UIActionHandler* handler, const QString& name, const UIActionContext& ctx,
      std::function<void(const UIActionContext&)>& action
  ) override;
};

} // namespace rewind_ui
