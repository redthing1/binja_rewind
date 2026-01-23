#include "rewind/ui/widgets/sidebar_widget.hpp"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVariant>

#include "rewind/ui/actions/action_router.hpp"
#include "rewind/ui/theme/icon_loader.hpp"
#include "rewind/ui/widgets/controls_widget.hpp"
#include "rewind/ui/widgets/elided_label.hpp"
#include "rewind/ui/widgets/registers_view.hpp"
#include "rewind/ui/widgets/session_view.hpp"
#include "rewind/ui/widgets/trace_slice_view.hpp"
#include "theme.h"

namespace {

QLabel* make_value_label(QWidget* parent) {
  auto* label = new QLabel(parent);
  label->setWordWrap(false);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  return label;
}

} // namespace

namespace binja::rewind::ui {

RewindSidebarWidget::RewindSidebarWidget(const QString& name, ViewFrame* frame, BinaryViewRef data)
    : SidebarWidget(name), m_data(data), m_frame(frame) {
  build_ui();
  controller_ = std::make_unique<SidebarController>(*this, m_data, m_frame);
  wire_controls();
  binja::rewind::ui::RewindActionRouter::refresh_bindings();
}

RewindSidebarWidget::~RewindSidebarWidget() = default;

void RewindSidebarWidget::notifyFontChanged() {
  if (m_trace_path) {
    m_trace_path->setFullText(m_trace_path->fullText());
  }
  if (controller_) {
    controller_->notify_font_changed();
  }
}

void RewindSidebarWidget::notifyThemeChanged() {
  if (m_status) {
    auto status_color = getThemeColor(CommentColor);
    m_status->setStyleSheet(QString("color: %1;").arg(status_color.name()));
  }
  if (controller_) {
    controller_->notify_theme_changed();
  }
}

void RewindSidebarWidget::set_status(const QString& text) { m_status->setText(text); }

void RewindSidebarWidget::set_trace_path(const QString& text) { m_trace_path->setFullText(text); }

void RewindSidebarWidget::set_position(const QString& text) { m_position->setText(text); }

void RewindSidebarWidget::set_controls_enabled(bool enabled) {
  if (m_controls) {
    m_controls->set_controls_enabled(enabled);
  }
}

void RewindSidebarWidget::set_trace_loaded(bool loaded) {
  if (m_controls) {
    m_controls->set_trace_loaded(loaded);
  }
}

void RewindSidebarWidget::set_thread_list(
    const std::vector<core::model::ThreadInfo>& threads, uint64_t current_thread
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

void RewindSidebarWidget::set_registers(const std::vector<core::model::RegisterValue>& registers) {
  if (!m_registers_view) {
    return;
  }
  m_registers_view->setRegisters(registers);
}

void RewindSidebarWidget::set_trace_entries(const std::vector<TraceSliceEntry>& entries, int current_row) {
  if (!m_trace_slice_view) {
    return;
  }
  if (entries.empty()) {
    m_trace_slice_view->clear();
    return;
  }
  m_trace_slice_view->setEntries(entries);
  if (current_row >= 0) {
    m_trace_slice_view->selectRow(current_row);
  }
}

void RewindSidebarWidget::set_session_summary(const core::model::TraceSummary& summary) {
  if (m_session_view) {
    m_session_view->setSummary(summary);
  }
}

void RewindSidebarWidget::set_session_modules(const std::vector<core::model::TraceModule>& modules) {
  if (m_session_view) {
    m_session_view->setModules(modules);
  }
}

void RewindSidebarWidget::reset_trace_ui() {
  set_trace_loaded(false);
  set_trace_path("No Trace Loaded");
  set_position("- / -");
  set_status("Idle");
  set_controls_enabled(false);
  if (m_thread_selector) {
    m_thread_selector->clear();
    m_thread_selector->addItem("No Trace Loaded");
    m_thread_selector->setEnabled(false);
  }
  if (m_registers_view) {
    m_registers_view->clear();
  }
  if (m_trace_slice_view) {
    m_trace_slice_view->clear();
  }
  if (m_session_view) {
    m_session_view->clear();
  }
}

void RewindSidebarWidget::refresh_viewports() {
  if (m_registers_view) {
    m_registers_view->viewport()->update();
  }
  if (m_trace_slice_view) {
    m_trace_slice_view->viewport()->update();
  }
  if (m_session_view) {
    m_session_view->update();
  }
}

QWidget* RewindSidebarWidget::widget() { return this; }

void RewindSidebarWidget::build_ui() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  auto* splitter = new QSplitter(Qt::Vertical, this);
  splitter->setChildrenCollapsible(false);

  auto* header_panel = new QWidget(this);
  auto* header_layout = new QVBoxLayout(header_panel);
  header_layout->setContentsMargins(0, 0, 0, 0);
  header_layout->setSpacing(6);

  m_controls = new RewindControlsWidget(this);
  m_controls->set_controls_enabled(false);
  header_layout->addWidget(m_controls);

  auto* info_grid = new QGridLayout();
  info_grid->setContentsMargins(0, 0, 0, 0);
  info_grid->setHorizontalSpacing(8);
  info_grid->setVerticalSpacing(4);

  auto* trace_label = new QLabel("Trace", this);
  m_trace_path = new ElidedLabel(this);
  m_trace_path->setFullText("No Trace Loaded");

  auto* thread_label = new QLabel("Thread", this);
  m_thread_selector = new QComboBox(this);
  m_thread_selector->addItem("No Trace Loaded");
  m_thread_selector->setEnabled(false);
  QObject::connect(m_thread_selector, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
    if (!controller_ || index < 0) {
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
    controller_->on_thread_selected(thread_id);
  });

  auto* position_label = new QLabel("Position", this);
  m_position = make_value_label(this);
  m_position->setText("- / -");

  info_grid->addWidget(trace_label, 0, 0);
  info_grid->addWidget(m_trace_path, 0, 1, 1, 3);
  info_grid->addWidget(thread_label, 1, 0);
  info_grid->addWidget(m_thread_selector, 1, 1);
  info_grid->addWidget(position_label, 1, 2);
  info_grid->addWidget(m_position, 1, 3);

  header_layout->addLayout(info_grid);

  m_status = make_value_label(this);
  m_status->setText("Idle");
  auto status_color = getThemeColor(CommentColor);
  m_status->setStyleSheet(QString("color: %1;").arg(status_color.name()));
  header_layout->addWidget(m_status);

  m_tabs = new QTabWidget(this);

  auto* registers_tab = new QWidget(this);
  auto* registers_layout = new QVBoxLayout(registers_tab);
  registers_layout->setContentsMargins(6, 6, 6, 6);
  registers_layout->setSpacing(6);
  m_registers_view = new RegistersView(registers_tab);
  registers_layout->addWidget(m_registers_view);

  auto* trace_tab = new QWidget(this);
  auto* trace_layout = new QVBoxLayout(trace_tab);
  trace_layout->setContentsMargins(6, 6, 6, 6);
  trace_layout->setSpacing(6);
  m_trace_slice_view = new TraceSliceView(trace_tab);
  trace_layout->addWidget(m_trace_slice_view);
  QObject::connect(m_trace_slice_view, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
    if (!index.isValid() || !controller_) {
      return;
    }
    controller_->on_trace_slice_double_clicked(index.row());
  });

  m_session_view = new SessionView(this);

  m_tabs->addTab(registers_tab, "Registers");
  m_tabs->addTab(trace_tab, "Trace");
  m_tabs->addTab(m_session_view, "Session");

  splitter->addWidget(header_panel);
  splitter->addWidget(m_tabs);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);

  layout->addWidget(splitter);
  setLayout(layout);
}

void RewindSidebarWidget::wire_controls() {
  m_controls->set_on_run_start([this]() {
    if (controller_) {
      controller_->run_to_start();
    }
  });
  m_controls->set_on_play_forward([this]() {
    if (controller_) {
      controller_->run_forward();
    }
  });
  m_controls->set_on_pause([this]() {
    if (controller_) {
      controller_->pause();
    }
  });
  m_controls->set_on_step_in([this]() {
    if (controller_) {
      controller_->step_instruction();
    }
  });
  m_controls->set_on_step_over([this]() {
    if (controller_) {
      controller_->step_over();
    }
  });
  m_controls->set_on_step_out([this]() {
    if (controller_) {
      controller_->step_out();
    }
  });
  m_controls->set_on_step_back([this]() {
    if (controller_) {
      controller_->step_instruction_backward();
    }
  });
  m_controls->set_on_play_backward([this]() {
    if (controller_) {
      controller_->run_backward();
    }
  });
  m_controls->set_on_load_trace([this]() {
    if (controller_) {
      controller_->on_load_trace_requested();
    }
  });
  m_controls->set_on_clear_trace([this]() {
    if (controller_) {
      controller_->on_clear_trace_requested();
    }
  });
}

bool RewindSidebarWidget::can_handle_action(const QString& action_name, const UIActionContext& context) const {
  if (!controller_) {
    return false;
  }
  return controller_->can_handle_action(action_name, context);
}

bool RewindSidebarWidget::handle_action(const QString& action_name, const UIActionContext& context) {
  if (!controller_) {
    return false;
  }
  return controller_->handle_action(action_name, context);
}

RewindSidebarWidgetType::RewindSidebarWidgetType()
    : SidebarWidgetType(binja::rewind::ui::make_icon_image(":/rewind/icons/play-back.svg"), "Rewind") {}

SidebarWidget* RewindSidebarWidgetType::createWidget(ViewFrame* frame, BinaryViewRef data) {
  return new RewindSidebarWidget("Rewind", frame, data);
}

SidebarWidgetLocation RewindSidebarWidgetType::defaultLocation() const { return SidebarWidgetLocation::LeftContent; }

SidebarContextSensitivity RewindSidebarWidgetType::contextSensitivity() const { return PerViewTypeSidebarContext; }

SidebarIconVisibility RewindSidebarWidgetType::defaultIconVisibility() const { return HideSidebarIconIfNoContent; }

bool RewindSidebarWidgetType::isInReferenceArea() const { return false; }

} // namespace binja::rewind::ui
