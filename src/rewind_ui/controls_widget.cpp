#include "rewind_ui/controls_widget.hpp"

#include <QAction>
#include <QToolButton>

#include "rewind_ui/icon_loader.hpp"

namespace {

QAction* make_action(QObject* parent, const QString& text, const QIcon& icon, const QString& tooltip) {
  auto* action = new QAction(icon, text, parent);
  action->setToolTip(tooltip);
  return action;
}

} // namespace

RewindControlsWidget::RewindControlsWidget(QWidget* parent) : QToolBar(parent) {
  setStyleSheet("QToolBar{spacing:2px; border:none;}");
  setIconSize(QSize(18, 18));
  setMovable(false);
  setFloatable(false);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  setMinimumHeight(24);

  const auto cyan = getThemeColor(CyanStandardHighlightColor);
  const auto green = getThemeColor(GreenStandardHighlightColor);
  const auto red = getThemeColor(RedStandardHighlightColor);
  const auto white = getThemeColor(WhiteStandardHighlightColor);

  action_play_forward_ =
      make_action(this, "Play Forward", rewind_ui::make_icon(":/rewind/icons/play.svg", green), "Play Forward");
  action_pause_ = make_action(this, "Pause", rewind_ui::make_icon(":/rewind/icons/pause.svg", white), "Pause");
  action_step_in_ =
      make_action(this, "Step In", rewind_ui::make_icon(":/rewind/icons/arrow-down-to-dot.svg", cyan), "Step In");
  action_step_over_ =
      make_action(this, "Step Over", rewind_ui::make_icon(":/rewind/icons/redo-dot.svg", cyan), "Step Over");
  action_step_out_ =
      make_action(this, "Step Out", rewind_ui::make_icon(":/rewind/icons/arrow-up-from-dot.svg", cyan), "Step Out");
  action_step_back_ =
      make_action(this, "Step Back", rewind_ui::make_icon(":/rewind/icons/undo-dot.svg", red), "Step Back");
  action_play_backward_ =
      make_action(this, "Play Backward", rewind_ui::make_icon(":/rewind/icons/play-back.svg", red), "Play Backward");
  action_run_start_ =
      make_action(this, "Run to Start", rewind_ui::make_icon(":/rewind/icons/rotate-ccw.svg", red), "Run to Start");
  action_load_trace_ =
      make_action(this, "Load Trace", rewind_ui::make_icon(":/rewind/icons/folder-open.svg", white), "Load Trace");
  action_clear_trace_ =
      make_action(this, "Clear Trace", rewind_ui::make_icon(":/rewind/icons/trash-2.svg", red), "Clear Trace");

  addAction(action_play_forward_);
  addAction(action_pause_);
  addAction(action_step_in_);
  addAction(action_step_over_);
  addAction(action_step_out_);
  addSeparator();
  addAction(action_step_back_);
  addAction(action_play_backward_);
  addAction(action_run_start_);
  addSeparator();
  addAction(action_load_trace_);
  addAction(action_clear_trace_);

  auto style_button = [this](QAction* action) {
    if (auto* button = qobject_cast<QToolButton*>(widgetForAction(action))) {
      button->setToolButtonStyle(Qt::ToolButtonIconOnly);
      button->setAutoRaise(true);
    }
  };

  style_button(action_run_start_);
  style_button(action_play_forward_);
  style_button(action_pause_);
  style_button(action_step_in_);
  style_button(action_step_over_);
  style_button(action_step_out_);
  style_button(action_step_back_);
  style_button(action_play_backward_);
  style_button(action_load_trace_);
  style_button(action_clear_trace_);

  set_trace_loaded(false);

  connect(action_run_start_, &QAction::triggered, this, [this]() {
    if (on_run_start_) {
      on_run_start_();
    }
  });

  connect(action_play_forward_, &QAction::triggered, this, [this]() {
    if (on_play_forward_) {
      on_play_forward_();
    }
  });

  connect(action_pause_, &QAction::triggered, this, [this]() {
    if (on_pause_) {
      on_pause_();
    }
  });

  connect(action_step_in_, &QAction::triggered, this, [this]() {
    if (on_step_in_) {
      on_step_in_();
    }
  });

  connect(action_step_over_, &QAction::triggered, this, [this]() {
    if (on_step_over_) {
      on_step_over_();
    }
  });

  connect(action_step_out_, &QAction::triggered, this, [this]() {
    if (on_step_out_) {
      on_step_out_();
    }
  });

  connect(action_step_back_, &QAction::triggered, this, [this]() {
    if (on_step_back_) {
      on_step_back_();
    }
  });

  connect(action_play_backward_, &QAction::triggered, this, [this]() {
    if (on_play_backward_) {
      on_play_backward_();
    }
  });

  connect(action_load_trace_, &QAction::triggered, this, [this]() {
    if (on_load_trace_) {
      on_load_trace_();
    }
  });

  connect(action_clear_trace_, &QAction::triggered, this, [this]() {
    if (on_clear_trace_) {
      on_clear_trace_();
    }
  });
}

void RewindControlsWidget::set_controls_enabled(bool enabled) {
  action_run_start_->setEnabled(enabled);
  action_play_forward_->setEnabled(enabled);
  action_pause_->setEnabled(enabled);
  action_step_in_->setEnabled(enabled);
  action_step_over_->setEnabled(enabled);
  action_step_out_->setEnabled(enabled);
  action_step_back_->setEnabled(enabled);
  action_play_backward_->setEnabled(enabled);
  action_clear_trace_->setEnabled(enabled);
}

void RewindControlsWidget::set_trace_loaded(bool loaded) {
  action_load_trace_->setVisible(!loaded);
  action_clear_trace_->setVisible(loaded);
}
