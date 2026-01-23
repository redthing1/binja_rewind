#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "binaryninjaapi.h"
#include "rewind/core/model/replay_types.hpp"
#include "rewind/ui/theme/gradient_painter.hpp"
#include "rewind/ui/widgets/trace_slice_view.hpp"

class QString;
class UIActionContext;
class UIContext;
class View;
class ViewFrame;
class QWidget;

namespace binja::rewind::core {
class RewindWorker;
} // namespace binja::rewind::core

namespace binja::rewind::ui {

class SidebarView {
public:
  virtual ~SidebarView() = default;

  virtual void set_status(const QString& text) = 0;
  virtual void set_trace_path(const QString& text) = 0;
  virtual void set_position(const QString& text) = 0;
  virtual void set_controls_enabled(bool enabled) = 0;
  virtual void set_trace_loaded(bool loaded) = 0;
  virtual void set_thread_list(const std::vector<core::model::ThreadInfo>& threads, uint64_t current_thread) = 0;
  virtual void set_registers(const std::vector<core::model::RegisterValue>& regs) = 0;
  virtual void set_trace_entries(const std::vector<TraceSliceEntry>& entries, int current_row) = 0;
  virtual void set_session_summary(const core::model::TraceSummary& summary) = 0;
  virtual void set_session_modules(const std::vector<core::model::TraceModule>& modules) = 0;
  virtual void reset_trace_ui() = 0;
  virtual void refresh_viewports() = 0;
  virtual QWidget* widget() = 0;
};

class SidebarController {
public:
  SidebarController(SidebarView& view, BinaryViewRef data, ViewFrame* frame);
  ~SidebarController();

  void on_load_trace_requested();
  void on_clear_trace_requested();
  void on_thread_selected(uint64_t thread_id);
  void on_trace_slice_double_clicked(int row);

  void run_to_start();
  void run_forward();
  void run_backward();
  void pause();
  void step_instruction();
  void step_instruction_backward();
  void step_over();
  void step_out();

  bool can_handle_action(const QString& action_name, const UIActionContext& context) const;
  bool handle_action(const QString& action_name, const UIActionContext& context);

  void notify_font_changed();
  void notify_theme_changed();

private:
  void setup_worker();
  void refresh_frontier_setting();
  void refresh_reverse_history_setting();
  void apply_update(const core::model::ReplayUpdate& update);
  void update_threads(const std::vector<core::model::ThreadInfo>& threads, uint64_t current_thread);
  void update_position(const core::model::ReplayUpdate& update);
  void update_registers(const std::vector<core::model::RegisterValue>& registers);
  void update_trace_slice(const core::model::ReplayUpdate& update);
  void update_session_view(const core::model::ReplayUpdate& update);
  void update_navigation(const core::model::ReplayUpdate& update);

  bool navigate_to_address(uint64_t address);
  bool resolve_navigation_context(UIContext*& context, ViewFrame*& frame, View*& view);
  bool navigate_sync_groups(ViewFrame* frame, uint64_t address);
  bool ensure_function_at(uint64_t address);

  QString format_disasm(uint64_t address) const;

  SidebarView& view_;
  BinaryViewRef data_;
  ViewFrame* frame_ = nullptr;
  BinaryNinja::Ref<BinaryNinja::Logger> logger_;

  std::unique_ptr<core::RewindWorker> worker_;
  std::unique_ptr<GradientPainter> gradient_;

  std::string trace_path_value_;
  size_t frontier_size_ = 8;
  size_t reverse_history_size_ = 65536;
  std::optional<uint64_t> last_nav_address_;
  std::vector<TraceSliceEntry> trace_entries_;
  bool trace_loaded_ = false;
  bool controls_enabled_ = false;
};

} // namespace binja::rewind::ui
