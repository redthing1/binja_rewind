#include "rewind_ui/sidebar_widget.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "rewind_ui/controls_widget.hpp"
#include "rewind_ui/icon_loader.hpp"
#include "theme.h"

namespace {

QLabel* make_value_label(QWidget* parent) {
  auto* label = new QLabel(parent);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  return label;
}

QWidget* make_placeholder_tab(QWidget* parent, const QString& text) {
  auto* widget = new QWidget(parent);
  auto* layout = new QVBoxLayout(widget);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);

  auto* label = new QLabel(text, widget);
  label->setWordWrap(true);
  layout->addWidget(label);
  layout->addStretch(1);

  return widget;
}

} // namespace

RewindSidebarWidget::RewindSidebarWidget(const QString& name, ViewFrame* frame, BinaryViewRef data)
    : SidebarWidget(name), m_data(data), m_frame(frame) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->setAlignment(Qt::AlignTop);

  m_thread_selector = new QComboBox(this);
  m_thread_selector->addItem("No trace loaded");
  m_thread_selector->setEnabled(false);
  layout->addWidget(m_thread_selector);

  m_splitter = new QSplitter(Qt::Vertical, this);
  m_splitter->setChildrenCollapsible(true);
  m_splitter->setStretchFactor(0, 0);
  m_splitter->setStretchFactor(1, 1);

  auto* controls_panel = new QWidget(this);
  auto* controls_layout = new QVBoxLayout(controls_panel);
  controls_layout->setContentsMargins(6, 6, 6, 6);
  controls_layout->setSpacing(6);

  controls_panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  m_controls = new RewindControlsWidget(this);
  m_controls->set_controls_enabled(false);
  controls_layout->addWidget(m_controls);

  auto* info_form = new QFormLayout();
  info_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
  info_form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
  info_form->setHorizontalSpacing(10);
  info_form->setVerticalSpacing(6);

  m_trace_path = make_value_label(this);
  m_trace_path->setText("No trace loaded");

  m_position = make_value_label(this);
  m_position->setText("- / -");

  m_status = make_value_label(this);
  m_status->setText("Idle");

  info_form->addRow("Trace", m_trace_path);
  info_form->addRow("Position", m_position);
  info_form->addRow("Status", m_status);

  controls_layout->addLayout(info_form);

  m_tabs = new QTabWidget(this);

  auto* registers_tab = new QWidget(this);
  auto* registers_layout = new QVBoxLayout(registers_tab);
  registers_layout->setContentsMargins(8, 8, 8, 8);
  registers_layout->setSpacing(6);

  auto* registers_table = new QTableWidget(0, 2, registers_tab);
  registers_table->setHorizontalHeaderLabels({"Register", "Value"});
  registers_table->horizontalHeader()->setStretchLastSection(true);
  registers_table->verticalHeader()->setVisible(false);
  registers_table->setSelectionMode(QAbstractItemView::NoSelection);
  registers_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  registers_layout->addWidget(registers_table);

  auto* trace_tab = new QWidget(this);
  auto* trace_layout = new QVBoxLayout(trace_tab);
  trace_layout->setContentsMargins(8, 8, 8, 8);
  trace_layout->setSpacing(6);

  auto* trace_list = new QListWidget(trace_tab);
  trace_list->addItem("No trace loaded");
  trace_layout->addWidget(trace_list);

  auto* session_tab = make_placeholder_tab(this, "Load a w1r trace to populate registers, threads, and timeline data.");

  m_tabs->addTab(registers_tab, "Registers");
  m_tabs->addTab(trace_tab, "Trace");
  m_tabs->addTab(session_tab, "Session");

  m_splitter->addWidget(controls_panel);
  m_splitter->addWidget(m_tabs);

  layout->addWidget(m_splitter);
  setLayout(layout);

  m_controls->set_on_run_start([this]() { set_status("Run to start (stub)"); });
  m_controls->set_on_play_forward([this]() { set_status("Play forward (stub)"); });
  m_controls->set_on_pause([this]() { set_status("Paused (stub)"); });
  m_controls->set_on_step_in([this]() { set_status("Step in (stub)"); });
  m_controls->set_on_step_over([this]() { set_status("Step over (stub)"); });
  m_controls->set_on_step_out([this]() { set_status("Step out (stub)"); });
  m_controls->set_on_step_back([this]() { set_status("Step back (stub)"); });
  m_controls->set_on_play_backward([this]() { set_status("Play backward (stub)"); });
  m_controls->set_on_load_trace([this]() { on_load_trace(); });
  m_controls->set_on_clear_trace([this]() { set_status("Trace cleared (stub)"); });
}

RewindSidebarWidget::~RewindSidebarWidget() = default;

void RewindSidebarWidget::notifyFontChanged() {
  // placeholder for future font updates
}

void RewindSidebarWidget::set_status(const QString& text) { m_status->setText(text); }

void RewindSidebarWidget::set_trace_path(const QString& text) { m_trace_path->setText(text); }

void RewindSidebarWidget::set_position(const QString& text) { m_position->setText(text); }

void RewindSidebarWidget::on_load_trace() {
  const QString path = QFileDialog::getOpenFileName(
      this, "Open w1rewind trace", QString(), "Trace Files (*.w1rwnd *.w1r);;All Files (*)"
  );
  if (path.isEmpty()) {
    return;
  }

  m_trace_path_value = path.toStdString();
  set_trace_path(path);
  set_position("0 / 0");
  set_status("Trace loaded (UI only)");
  m_controls->set_controls_enabled(true);

  m_thread_selector->clear();
  m_thread_selector->addItem("Thread 1 (stub)");
  m_thread_selector->setEnabled(true);
}

RewindSidebarWidgetType::RewindSidebarWidgetType()
    : SidebarWidgetType(
          rewind_ui::make_pixmap(":/rewind/icons/play-back.svg", SidebarActiveIconColor).toImage(), "Rewind"
      ) {}

SidebarWidget* RewindSidebarWidgetType::createWidget(ViewFrame* frame, BinaryViewRef data) {
  return new RewindSidebarWidget("Rewind", frame, data);
}

SidebarWidgetLocation RewindSidebarWidgetType::defaultLocation() const { return SidebarWidgetLocation::RightBottom; }

SidebarContextSensitivity RewindSidebarWidgetType::contextSensitivity() const { return PerViewTypeSidebarContext; }

SidebarIconVisibility RewindSidebarWidgetType::defaultIconVisibility() const { return HideSidebarIconIfNoContent; }

bool RewindSidebarWidgetType::isInReferenceArea() const { return false; }
