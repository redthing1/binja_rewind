#pragma once

#include <functional>

#include <QToolBar>

#include "theme.h"

class QAction;

class RewindControlsWidget : public QToolBar {
public:
  using Callback = std::function<void()>;

  explicit RewindControlsWidget(QWidget* parent = nullptr);

  void set_on_run_start(Callback cb) { on_run_start_ = std::move(cb); }
  void set_on_play_forward(Callback cb) { on_play_forward_ = std::move(cb); }
  void set_on_pause(Callback cb) { on_pause_ = std::move(cb); }
  void set_on_step_in(Callback cb) { on_step_in_ = std::move(cb); }
  void set_on_step_over(Callback cb) { on_step_over_ = std::move(cb); }
  void set_on_step_out(Callback cb) { on_step_out_ = std::move(cb); }
  void set_on_step_back(Callback cb) { on_step_back_ = std::move(cb); }
  void set_on_play_backward(Callback cb) { on_play_backward_ = std::move(cb); }
  void set_on_load_trace(Callback cb) { on_load_trace_ = std::move(cb); }
  void set_on_clear_trace(Callback cb) { on_clear_trace_ = std::move(cb); }

  void set_controls_enabled(bool enabled);
  void set_trace_loaded(bool loaded);

private:
  QAction* action_run_start_ = nullptr;
  QAction* action_play_forward_ = nullptr;
  QAction* action_pause_ = nullptr;
  QAction* action_step_in_ = nullptr;
  QAction* action_step_over_ = nullptr;
  QAction* action_step_out_ = nullptr;
  QAction* action_step_back_ = nullptr;
  QAction* action_play_backward_ = nullptr;
  QAction* action_load_trace_ = nullptr;
  QAction* action_clear_trace_ = nullptr;

  Callback on_run_start_;
  Callback on_play_forward_;
  Callback on_pause_;
  Callback on_step_in_;
  Callback on_step_over_;
  Callback on_step_out_;
  Callback on_step_back_;
  Callback on_play_backward_;
  Callback on_load_trace_;
  Callback on_clear_trace_;
};
