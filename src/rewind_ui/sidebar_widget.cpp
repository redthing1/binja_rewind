#include "rewind_ui/sidebar_widget.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QVariant>

#include "rewind_core/worker.hpp"
#include "rewind_ui/controls_widget.hpp"
#include "rewind_ui/gradient_painter.hpp"
#include "rewind_ui/icon_loader.hpp"
#include "rewind_ui/rewind_settings.hpp"
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
  build_ui();
  setup_worker();
  wire_controls();
  refresh_frontier_setting();
}

RewindSidebarWidget::~RewindSidebarWidget() = default;

void RewindSidebarWidget::notifyFontChanged() {
  // placeholder for future font updates
}

void RewindSidebarWidget::notifyThemeChanged() {
  refresh_frontier_setting();
  if (m_worker && !m_trace_path_value.empty()) {
    m_worker->load_trace(m_trace_path_value);
  }
}

void RewindSidebarWidget::set_status(const QString& text) { m_status->setText(text); }

void RewindSidebarWidget::set_trace_path(const QString& text) {
  m_trace_path->setText(text);
  m_trace_path->setToolTip(text);
}

void RewindSidebarWidget::set_position(const QString& text) { m_position->setText(text); }

void RewindSidebarWidget::set_address(const QString& text) { m_address->setText(text); }

void RewindSidebarWidget::refresh_frontier_setting() {
  size_t frontier = rewind_ui::get_frontier_size(m_data);
  if (frontier == m_frontier_size) {
    return;
  }
  m_frontier_size = frontier;
  if (m_gradient) {
    m_gradient->set_frontier_size(frontier);
  }
  if (m_worker) {
    m_worker->set_gradient_size(frontier);
  }
}

void RewindSidebarWidget::build_ui() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(8);
  layout->setAlignment(Qt::AlignTop);

  auto* header_panel = new QWidget(this);
  auto* header_layout = new QVBoxLayout(header_panel);
  header_layout->setContentsMargins(0, 0, 0, 0);
  header_layout->setSpacing(8);
  header_panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  m_controls = new RewindControlsWidget(this);
  m_controls->set_controls_enabled(false);
  header_layout->addWidget(m_controls);

  auto* info_form = new QFormLayout();
  info_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
  info_form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
  info_form->setHorizontalSpacing(10);
  info_form->setVerticalSpacing(4);

  m_trace_path = make_value_label(this);
  m_trace_path->setText("No Trace Loaded");

  m_thread_selector = new QComboBox(this);
  m_thread_selector->addItem("No Trace Loaded");
  m_thread_selector->setEnabled(false);
  QObject::connect(m_thread_selector, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
    on_thread_selected(index);
  });

  m_position = make_value_label(this);
  m_position->setText("- / -");

  m_address = make_value_label(this);
  m_address->setText("-");

  m_status = make_value_label(this);
  m_status->setText("Idle");

  info_form->addRow("Trace", m_trace_path);
  info_form->addRow("Thread", m_thread_selector);
  info_form->addRow("Position", m_position);
  info_form->addRow("Address", m_address);
  info_form->addRow("Status", m_status);

  header_layout->addLayout(info_form);

  m_tabs = new QTabWidget(this);

  auto* registers_tab = new QWidget(this);
  auto* registers_layout = new QVBoxLayout(registers_tab);
  registers_layout->setContentsMargins(8, 6, 8, 8);
  registers_layout->setSpacing(6);

  m_registers_table = new QTableWidget(0, 2, registers_tab);
  m_registers_table->setHorizontalHeaderLabels({"Register", "Value"});
  m_registers_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_registers_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_registers_table->verticalHeader()->setVisible(false);
  m_registers_table->setSelectionMode(QAbstractItemView::NoSelection);
  m_registers_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_registers_table->setAlternatingRowColors(true);
  registers_layout->addWidget(m_registers_table);

  auto* trace_tab = new QWidget(this);
  auto* trace_layout = new QVBoxLayout(trace_tab);
  trace_layout->setContentsMargins(8, 6, 8, 8);
  trace_layout->setSpacing(6);

  auto* trace_list = new QListWidget(trace_tab);
  trace_list->addItem("No Trace Loaded");
  trace_layout->addWidget(trace_list);

  auto* session_tab = make_placeholder_tab(this, "Load a w1r trace to populate registers, threads, and timeline data.");

  m_tabs->addTab(registers_tab, "Registers");
  m_tabs->addTab(trace_tab, "Trace");
  m_tabs->addTab(session_tab, "Session");

  layout->addWidget(header_panel);
  layout->addWidget(m_tabs, 1);
  setLayout(layout);
}

void RewindSidebarWidget::setup_worker() {
  m_gradient = std::make_unique<GradientPainter>(m_frontier_size);

  QPointer<RewindSidebarWidget> self(this);
  m_worker = std::make_unique<binja_rewind::RewindWorker>(m_data, [self](const binja_rewind::ReplayUpdate& update) {
    if (!self) {
      return;
    }
    self->apply_update(update);
  });
}

void RewindSidebarWidget::wire_controls() {
  m_controls->set_on_run_start([this]() {
    if (m_worker) {
      m_worker->run_to_start();
    }
  });
  m_controls->set_on_play_forward([this]() {
    if (m_worker) {
      m_worker->run_forward();
    }
  });
  m_controls->set_on_pause([this]() {
    if (m_worker) {
      m_worker->pause();
    }
  });
  m_controls->set_on_step_in([this]() {
    if (m_worker) {
      m_worker->step_instruction();
    }
  });
  m_controls->set_on_step_over([this]() {
    if (m_worker) {
      m_worker->step_over();
    }
  });
  m_controls->set_on_step_out([this]() {
    if (m_worker) {
      m_worker->step_out();
    }
  });
  m_controls->set_on_step_back([this]() {
    if (m_worker) {
      m_worker->step_instruction_backward();
    }
  });
  m_controls->set_on_play_backward([this]() {
    if (m_worker) {
      m_worker->run_backward();
    }
  });
  m_controls->set_on_load_trace([this]() { on_load_trace(); });
  m_controls->set_on_clear_trace([this]() { on_clear_trace(); });
}

void RewindSidebarWidget::reset_trace_ui() {
  m_controls->set_trace_loaded(false);
  set_trace_path("No Trace Loaded");
  set_position("- / -");
  set_address("-");
  m_controls->set_controls_enabled(false);
  if (m_thread_selector) {
    m_thread_selector->clear();
    m_thread_selector->addItem("No Trace Loaded");
    m_thread_selector->setEnabled(false);
  }
  if (m_registers_table) {
    m_registers_table->setRowCount(0);
  }
  if (m_gradient) {
    m_gradient->clear(m_data);
  }
}

void RewindSidebarWidget::update_threads(
    const std::vector<binja_rewind::ThreadInfo>& threads, uint64_t current_thread
) {
  if (!m_thread_selector) {
    return;
  }
  if (!threads.empty()) {
    QSignalBlocker blocker(m_thread_selector);
    m_thread_selector->clear();
    for (const auto& thread : threads) {
      QString label;
      if (thread.name.empty()) {
        label = QString("Thread %1").arg(thread.id);
      } else {
        label = QString("%1 (%2)").arg(QString::fromStdString(thread.name)).arg(thread.id);
      }
      m_thread_selector->addItem(label, QVariant::fromValue<qulonglong>(thread.id));
    }
    m_thread_selector->setEnabled(true);
  }

  if (current_thread != 0) {
    int idx = m_thread_selector->findData(QVariant::fromValue<qulonglong>(current_thread));
    if (idx >= 0) {
      QSignalBlocker blocker(m_thread_selector);
      m_thread_selector->setCurrentIndex(idx);
    }
  }
}

void RewindSidebarWidget::update_position_and_address(const binja_rewind::ReplayUpdate& update) {
  if (update.has_position) {
    set_position(QString::number(update.sequence));
    uint64_t addr = update.view_address.value_or(update.trace_address);
    set_address(QString("0x%1").arg(addr, 0, 16));
  } else if (update.trace_loaded) {
    set_position("- / -");
    set_address("-");
  }
}

void RewindSidebarWidget::update_registers(const std::vector<binja_rewind::RegisterValue>& registers) {
  if (!m_registers_table) {
    return;
  }
  m_registers_table->setRowCount(static_cast<int>(registers.size()));
  for (int row = 0; row < static_cast<int>(registers.size()); ++row) {
    const auto& reg = registers[static_cast<size_t>(row)];
    auto* name_item = m_registers_table->item(row, 0);
    if (!name_item) {
      name_item = new QTableWidgetItem();
      m_registers_table->setItem(row, 0, name_item);
    }
    name_item->setText(QString::fromStdString(reg.name));

    auto* value_item = m_registers_table->item(row, 1);
    if (!value_item) {
      value_item = new QTableWidgetItem();
      value_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      m_registers_table->setItem(row, 1, value_item);
    }
    value_item->setText(QString::fromStdString(reg.value));
  }
}

void RewindSidebarWidget::update_navigation(const binja_rewind::ReplayUpdate& update) {
  if (update.view_address.has_value() && m_frame) {
    m_data->Navigate(m_frame->getCurrentView().toStdString(), *update.view_address);
  }

  if (m_gradient) {
    if (update.view_address.has_value()) {
      m_gradient->paint(m_data, update.view_address, update.past_addresses, update.future_addresses);
    } else {
      m_gradient->clear(m_data);
    }
  }
}

void RewindSidebarWidget::on_load_trace() {
  const QString path = QFileDialog::getOpenFileName(
      this, "Open w1rewind trace", QString(), "Trace Files (*.w1rwnd *.w1r);;All Files (*)"
  );
  if (path.isEmpty()) {
    return;
  }

  m_trace_path_value = path.toStdString();
  if (m_worker) {
    m_worker->load_trace(m_trace_path_value);
  }
}

void RewindSidebarWidget::on_clear_trace() {
  if (m_worker) {
    m_worker->clear_trace();
  }
}

void RewindSidebarWidget::on_thread_selected(int index) {
  if (!m_worker || index < 0) {
    return;
  }
  QVariant data = m_thread_selector->itemData(index);
  if (!data.isValid()) {
    return;
  }
  uint64_t thread_id = data.toULongLong();
  if (thread_id == 0) {
    return;
  }
  m_worker->select_thread(thread_id);
}

void RewindSidebarWidget::apply_update(const binja_rewind::ReplayUpdate& update) {
  if (!update.status.empty()) {
    set_status(QString::fromStdString(update.status));
  }

  if (update.trace_cleared) {
    reset_trace_ui();
    return;
  }

  if (update.trace_loaded) {
    m_controls->set_trace_loaded(true);
    set_trace_path(QString::fromStdString(update.trace_path));
  }

  m_controls->set_controls_enabled(update.controls_enabled);

  refresh_frontier_setting();

  update_threads(update.threads, update.thread_id);
  update_position_and_address(update);
  update_registers(update.registers);
  update_navigation(update);
}

RewindSidebarWidgetType::RewindSidebarWidgetType()
    : SidebarWidgetType(
          rewind_ui::make_pixmap(":/rewind/icons/arrow-left-from-line.svg", SidebarActiveIconColor).toImage(), "Rewind"
      ) {}

SidebarWidget* RewindSidebarWidgetType::createWidget(ViewFrame* frame, BinaryViewRef data) {
  return new RewindSidebarWidget("Rewind", frame, data);
}

SidebarWidgetLocation RewindSidebarWidgetType::defaultLocation() const { return SidebarWidgetLocation::RightBottom; }

SidebarContextSensitivity RewindSidebarWidgetType::contextSensitivity() const { return PerViewTypeSidebarContext; }

SidebarIconVisibility RewindSidebarWidgetType::defaultIconVisibility() const { return HideSidebarIconIfNoContent; }

bool RewindSidebarWidgetType::isInReferenceArea() const { return false; }
