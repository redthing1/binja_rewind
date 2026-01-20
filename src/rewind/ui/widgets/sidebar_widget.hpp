#pragma once

#include <memory>
#include <vector>

#include "action.h"
#include "binaryninjaapi.h"
#include "rewind/ui/controllers/sidebar_controller.hpp"
#include "sidebarwidget.h"
#include "uitypes.h"
#include "viewframe.h"

class QLabel;
class QComboBox;
class QTabWidget;
class QWidget;

namespace binja::rewind::ui {

class RewindControlsWidget;
class ElidedLabel;
class RegistersView;
class TraceSliceView;
class SessionView;

class RewindSidebarWidget : public SidebarWidget, public SidebarView {
public:
  RewindSidebarWidget(const QString& name, ViewFrame* frame, BinaryViewRef data);
  ~RewindSidebarWidget() override;

  void notifyFontChanged() override;
  void notifyThemeChanged() override;

  bool can_handle_action(const QString& action_name, const UIActionContext& context) const;
  bool handle_action(const QString& action_name, const UIActionContext& context);

  void set_status(const QString& text) override;
  void set_trace_path(const QString& text) override;
  void set_position(const QString& text) override;
  void set_controls_enabled(bool enabled) override;
  void set_trace_loaded(bool loaded) override;
  void set_thread_list(const std::vector<core::model::ThreadInfo>& threads, uint64_t current_thread) override;
  void set_registers(const std::vector<core::model::RegisterValue>& registers) override;
  void set_trace_entries(const std::vector<TraceSliceEntry>& entries, int current_row) override;
  void set_session_summary(const core::model::TraceSummary& summary) override;
  void set_session_modules(const std::vector<core::model::TraceModule>& modules) override;
  void reset_trace_ui() override;
  void refresh_viewports() override;
  QWidget* widget() override;

private:
  void build_ui();
  void wire_controls();

  BinaryViewRef m_data;
  ViewFrame* m_frame = nullptr;

  QComboBox* m_thread_selector = nullptr;
  QTabWidget* m_tabs = nullptr;
  RewindControlsWidget* m_controls = nullptr;

  RegistersView* m_registers_view = nullptr;
  TraceSliceView* m_trace_slice_view = nullptr;
  SessionView* m_session_view = nullptr;

  ElidedLabel* m_trace_path = nullptr;
  QLabel* m_status = nullptr;
  QLabel* m_position = nullptr;

  std::unique_ptr<SidebarController> controller_;
};

class RewindSidebarWidgetType : public SidebarWidgetType {
public:
  RewindSidebarWidgetType();

  SidebarWidget* createWidget(ViewFrame* frame, BinaryViewRef data) override;
  SidebarWidgetLocation defaultLocation() const override;
  SidebarContextSensitivity contextSensitivity() const override;
  SidebarIconVisibility defaultIconVisibility() const override;
  bool isInReferenceArea() const override;
};

} // namespace binja::rewind::ui
