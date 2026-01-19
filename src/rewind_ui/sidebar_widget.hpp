#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "binaryninjaapi.h"
#include "sidebarwidget.h"
#include "uitypes.h"
#include "viewframe.h"

class QLabel;
class QComboBox;
class QTabWidget;
class QTableView;
class UIContext;
class View;

class RewindControlsWidget;
class GradientPainter;
class ElidedLabel;
class RegistersView;
class TraceSliceView;
class SessionView;

namespace binja_rewind {
struct ThreadInfo;
struct RegisterValue;
struct ReplayUpdate;
class RewindWorker;
} // namespace binja_rewind

class RewindSidebarWidget : public SidebarWidget {
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

  std::string m_trace_path_value;
  size_t m_frontier_size = 8;
  std::optional<uint64_t> m_last_nav_address;
  std::unique_ptr<binja_rewind::RewindWorker> m_worker;
  std::unique_ptr<GradientPainter> m_gradient;
  BinaryNinja::Ref<BinaryNinja::Logger> logger_;

  void set_status(const QString& text);
  void set_trace_path(const QString& text);
  void set_position(const QString& text);
  void refresh_viewports();
  QString format_disasm(uint64_t address) const;
  void refresh_frontier_setting();
  void build_ui();
  void setup_worker();
  void wire_controls();
  void reset_trace_ui();
  bool navigate_to_address(uint64_t address);
  bool resolve_navigation_context(UIContext*& context, ViewFrame*& frame, View*& view);
  bool navigate_sync_groups(ViewFrame* frame, uint64_t address);
  bool ensure_function_at(uint64_t address);
  void update_threads(const std::vector<binja_rewind::ThreadInfo>& threads, uint64_t current_thread);
  void update_position(const binja_rewind::ReplayUpdate& update);
  void update_registers(const std::vector<binja_rewind::RegisterValue>& registers);
  void update_trace_slice(const binja_rewind::ReplayUpdate& update);
  void update_session_view(const binja_rewind::ReplayUpdate& update);
  void update_navigation(const binja_rewind::ReplayUpdate& update);
  void on_load_trace();
  void on_clear_trace();
  void on_thread_selected(int index);
  void apply_update(const binja_rewind::ReplayUpdate& update);

public:
  RewindSidebarWidget(const QString& name, ViewFrame* frame, BinaryViewRef data);
  ~RewindSidebarWidget() override;

  void notifyFontChanged() override;
  void notifyThemeChanged() override;
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
