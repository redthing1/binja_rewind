#pragma once

#include <memory>
#include <string>
#include <vector>

#include "binaryninjaapi.h"
#include "sidebarwidget.h"
#include "uitypes.h"
#include "viewframe.h"

class QLabel;
class QComboBox;
class QTabWidget;
class QTableWidget;

class RewindControlsWidget;
class GradientPainter;

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

  QTableWidget* m_registers_table = nullptr;

  QLabel* m_trace_path = nullptr;
  QLabel* m_status = nullptr;
  QLabel* m_position = nullptr;
  QLabel* m_address = nullptr;

  std::string m_trace_path_value;
  size_t m_frontier_size = 8;
  std::unique_ptr<binja_rewind::RewindWorker> m_worker;
  std::unique_ptr<GradientPainter> m_gradient;

  void set_status(const QString& text);
  void set_trace_path(const QString& text);
  void set_position(const QString& text);
  void set_address(const QString& text);
  void refresh_frontier_setting();
  void build_ui();
  void setup_worker();
  void wire_controls();
  void reset_trace_ui();
  void update_threads(const std::vector<binja_rewind::ThreadInfo>& threads, uint64_t current_thread);
  void update_position_and_address(const binja_rewind::ReplayUpdate& update);
  void update_registers(const std::vector<binja_rewind::RegisterValue>& registers);
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
