#include "rewind_ui/action_router.hpp"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QPointer>

#include "action.h"
#include "sidebar.h"
#include "rewind_ui/rewind_actions.hpp"
#include "rewind_ui/sidebar_widget.hpp"

namespace {

bool is_rewind_action(const QString& name) {
  return rewind_ui::action_id_from_name(name).has_value();
}

RewindSidebarWidget* active_rewind_widget(UIContext* context) {
  if (!context) {
    return nullptr;
  }
  Sidebar* sidebar = context->sidebar();
  if (!sidebar) {
    return nullptr;
  }
  SidebarWidgetType* type = Sidebar::typeFromName("Rewind");
  if (!type) {
    return nullptr;
  }
  if (!sidebar->isActive(type)) {
    return nullptr;
  }
  QWidget* widget = sidebar->widget(type);
  if (!widget) {
    return nullptr;
  }
  return dynamic_cast<RewindSidebarWidget*>(widget);
}

bool can_route_action(const QString& name, const UIActionContext& ctx) {
  if (!is_rewind_action(name)) {
    return false;
  }
  auto* widget = active_rewind_widget(ctx.context);
  if (!widget) {
    return false;
  }
  return widget->can_handle_action(name, ctx);
}

bool route_action(const QString& name, const UIActionContext& ctx) {
  auto* widget = active_rewind_widget(ctx.context);
  if (!widget) {
    return false;
  }
  return widget->handle_action(name, ctx);
}

UIActionContext build_action_context(RewindSidebarWidget* widget) {
  UIActionContext ctx{};
  if (!widget) {
    return ctx;
  }
  auto* context = UIContext::contextForWidget(widget);
  ctx.context = context;
  if (!context) {
    return ctx;
  }
  auto* frame = context->getCurrentViewFrame();
  if (!frame) {
    return ctx;
  }
  ctx.view = frame->getCurrentViewInterface();
  ctx.binaryView = frame->getCurrentBinaryView();
  ctx.address = frame->getCurrentOffset();
  return ctx;
}

class RewindReverseKeyInterceptor final : public QObject {
public:
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride) {
      return false;
    }
    auto* key_event = static_cast<QKeyEvent*>(event);
    const auto modifiers = key_event->modifiers();
    const auto key = static_cast<Qt::Key>(key_event->key());
    const QKeyCombination combo(modifiers, key);
    for (const auto& spec : rewind_ui::action_specs()) {
      if (!rewind_ui::is_reverse_action(spec.id)) {
        continue;
      }
      if (spec.key.count() == 0) {
        continue;
      }
      if (combo != spec.key[0]) {
        continue;
      }
      UIContext* context = UIContext::activeContext();
      if (!context) {
        return false;
      }
      auto* widget = active_rewind_widget(context);
      if (!widget) {
        return false;
      }
      UIActionContext ctx = build_action_context(widget);
      if (!widget->can_handle_action(QString(spec.name), ctx)) {
        return false;
      }
      widget->handle_action(QString(spec.name), ctx);
      return true;
    }
    return false;
  }
};

RewindReverseKeyInterceptor* g_key_filter = nullptr;

void register_action_fallbacks() {
  for (const auto& spec : rewind_ui::action_specs()) {
    if (!UIAction::isActionRegistered(spec.name)) {
      UIAction::registerAction(spec.name, spec.key);
    }
  }

  for (const auto& spec : rewind_ui::action_specs()) {
    if (rewind_ui::is_reverse_action(spec.id)) {
      if (UIAction::getKeyBinding(spec.name).isEmpty()) {
        UIAction::registerAction(spec.name, spec.key);
        UIAction::resetKeyBindingToDefault(spec.name);
      }
      UIActionHandler::globalActions()->bindAction(
          spec.name,
          UIAction(
              [name = QString(spec.name)](const UIActionContext& ctx) { route_action(name, ctx); },
              [name = QString(spec.name)](const UIActionContext& ctx) { return can_route_action(name, ctx); }
          ),
          HighActionPriority
      );
      UIActionHandler::updateActionBindings(spec.name);
      continue;
    }

    if (UIActionHandler::isActionBoundToAnyHandler(spec.name)) {
      continue;
    }
    UIActionHandler::globalActions()->bindAction(
        spec.name,
        UIAction(
            [name = QString(spec.name)](const UIActionContext& ctx) { route_action(name, ctx); },
            [name = QString(spec.name)](const UIActionContext& ctx) { return can_route_action(name, ctx); }
        )
    );
    UIActionHandler::updateActionBindings(spec.name);
  }
}

} // namespace

namespace rewind_ui {

bool can_dispatch_action_for_view(const QString& name, BinaryNinja::BinaryView* view) {
  if (!view) {
    return false;
  }
  if (!is_rewind_action(name)) {
    return false;
  }
  UIContext* context = UIContext::activeContext();
  if (!context) {
    return false;
  }
  auto* widget = active_rewind_widget(context);
  if (!widget) {
    return false;
  }
  UIActionContext ctx = build_action_context(widget);
  if (ctx.binaryView != view) {
    return false;
  }
  return widget->can_handle_action(name, ctx);
}

bool dispatch_action_for_view(const QString& name, BinaryNinja::BinaryView* view) {
  if (!can_dispatch_action_for_view(name, view)) {
    return false;
  }
  UIContext* context = UIContext::activeContext();
  if (!context) {
    return false;
  }
  auto* widget = active_rewind_widget(context);
  if (!widget) {
    return false;
  }
  UIActionContext ctx = build_action_context(widget);
  return widget->handle_action(name, ctx);
}

static RewindActionRouter* g_router = nullptr;

void RewindActionRouter::init() {
  if (g_router) {
    return;
  }
  g_router = new RewindActionRouter();
  UIContext::registerNotification(g_router);
  register_action_fallbacks();
  if (!g_key_filter) {
    g_key_filter = new RewindReverseKeyInterceptor();
    if (auto* app = QCoreApplication::instance()) {
      app->installEventFilter(g_key_filter);
    }
  }
}

void RewindActionRouter::refresh_bindings() {
  if (!g_router) {
    init();
    return;
  }
  register_action_fallbacks();
}

void RewindActionRouter::OnActionExecuted(
    UIContext* context, UIActionHandler*, const QString& name, const UIActionContext& ctx,
    std::function<void(const UIActionContext&)>& action
) {
  if (!is_rewind_action(name)) {
    return;
  }

  auto* widget = active_rewind_widget(context);
  if (!widget) {
    return;
  }
  if (!widget->can_handle_action(name, ctx)) {
    return;
  }

  QPointer<RewindSidebarWidget> guard(widget);
  const QString action_name = name;
  action = [guard, action_name](const UIActionContext& inner_ctx) {
    if (!guard) {
      return;
    }
    guard->handle_action(action_name, inner_ctx);
  };
}

} // namespace rewind_ui
