#pragma once

#include <string>

#include "binaryninjaapi.h"
#include "sidebarwidget.h"
#include "uitypes.h"
#include "viewframe.h"

class QLabel;
class QComboBox;
class QSplitter;
class QTabWidget;

class RewindControlsWidget;

class RewindSidebarWidget : public SidebarWidget {
  BinaryViewRef m_data;
  ViewFrame* m_frame = nullptr;

  QComboBox* m_thread_selector = nullptr;
  QSplitter* m_splitter = nullptr;
  QTabWidget* m_tabs = nullptr;
  RewindControlsWidget* m_controls = nullptr;

  QLabel* m_trace_path = nullptr;
  QLabel* m_status = nullptr;
  QLabel* m_position = nullptr;

  std::string m_trace_path_value;

  void set_status(const QString& text);
  void set_trace_path(const QString& text);
  void set_position(const QString& text);
  void on_load_trace();

public:
  RewindSidebarWidget(const QString& name, ViewFrame* frame, BinaryViewRef data);
  ~RewindSidebarWidget() override;

  void notifyFontChanged() override;
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
