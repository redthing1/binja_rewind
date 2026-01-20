#include "rewind_ui/sidebar_widget.hpp"

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVariant>

#include "filecontext.h"
#include "rewind_core/worker.hpp"
#include "rewind_ui/controls_widget.hpp"
#include "rewind_ui/elided_label.hpp"
#include "rewind_ui/gradient_painter.hpp"
#include "rewind_ui/icon_loader.hpp"
#include "rewind_ui/registers_view.hpp"
#include "rewind_ui/action_router.hpp"
#include "rewind_ui/rewind_actions.hpp"
#include "rewind_ui/rewind_settings.hpp"
#include "rewind_ui/session_view.hpp"
#include "rewind_ui/trace_slice_view.hpp"
#include "syncgroup.h"
#include "theme.h"
#include "uicontext.h"

namespace {

QLabel* make_value_label(QWidget* parent) {
  auto* label = new QLabel(parent);
  label->setWordWrap(false);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  return label;
}

} // namespace

RewindSidebarWidget::RewindSidebarWidget(const QString& name, ViewFrame* frame, BinaryViewRef data)
    : SidebarWidget(name), m_data(data), m_frame(frame) {
  if (m_data) {
    logger_ = m_data->CreateLogger("Rewind");
  } else {
    logger_ = BinaryNinja::LogRegistry::CreateLogger("Rewind");
  }
  build_ui();
  setup_worker();
  wire_controls();
  rewind_ui::RewindActionRouter::refresh_bindings();
  refresh_frontier_setting();
}

RewindSidebarWidget::~RewindSidebarWidget() = default;

void RewindSidebarWidget::notifyFontChanged() {
  if (m_trace_path) {
    m_trace_path->setFullText(m_trace_path->fullText());
  }
  refresh_viewports();
}

void RewindSidebarWidget::notifyThemeChanged() {
  refresh_frontier_setting();
  if (m_status) {
    auto status_color = getThemeColor(CommentColor);
    m_status->setStyleSheet(QString("color: %1;").arg(status_color.name()));
  }
  refresh_viewports();
}

void RewindSidebarWidget::set_status(const QString& text) { m_status->setText(text); }

void RewindSidebarWidget::set_trace_path(const QString& text) { m_trace_path->setFullText(text); }

void RewindSidebarWidget::set_position(const QString& text) { m_position->setText(text); }

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

QString RewindSidebarWidget::format_disasm(uint64_t address) const {
  if (!m_data) {
    return QString("0x%1").arg(address, 0, 16);
  }
  auto arch = m_data->GetDefaultArchitecture();
  if (!arch) {
    return QString("0x%1").arg(address, 0, 16);
  }

  size_t max_len = arch->GetMaxInstructionLength();
  if (max_len == 0) {
    max_len = 16;
  }
  std::vector<uint8_t> bytes(max_len);
  size_t read = m_data->Read(bytes.data(), address, max_len);
  if (read == 0) {
    return QString("0x%1").arg(address, 0, 16);
  }

  size_t len = 0;
  std::vector<BinaryNinja::InstructionTextToken> tokens;
  if (arch->GetInstructionText(bytes.data(), address, len, tokens) && !tokens.empty()) {
    QString text;
    for (const auto& token : tokens) {
      text += QString::fromStdString(token.text);
    }
    QString line = text.simplified();
    if (!line.isEmpty()) {
      return QString("0x%1  %2").arg(address, 0, 16).arg(line);
    }
  }

  return QString("0x%1").arg(address, 0, 16);
}

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
    on_thread_selected(index);
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
    if (!index.isValid()) {
      return;
    }
    auto addr = m_trace_slice_view->address_for_row(index.row());
    if (!addr.has_value()) {
      return;
    }
    navigate_to_address(*addr);
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

bool RewindSidebarWidget::navigate_to_address(uint64_t address) {
  UIContext* context = nullptr;
  ViewFrame* frame = nullptr;
  View* view = nullptr;
  if (!resolve_navigation_context(context, frame, view)) {
    return false;
  }

  if (!ensure_function_at(address)) {
    return false;
  }

  bool navigated = false;
  if (view->getCurrentFunction()) {
    navigated = frame->navigate(m_data, address, true, true);
  } else {
    navigated = navigate_sync_groups(frame, address);
  }

  if (!navigated) {
    navigated = frame->navigate(m_data, address, true, true);
  }

  if (context) {
    context->refreshCurrentViewContents();
  }

  return navigated;
}

bool RewindSidebarWidget::resolve_navigation_context(UIContext*& context, ViewFrame*& frame, View*& view) {
  context = UIContext::contextForWidget(this);
  frame = m_frame ? m_frame : (context ? context->getCurrentViewFrame() : nullptr);
  if (!frame) {
    return false;
  }

  view = frame->getCurrentViewInterface();
  if (!view) {
    return false;
  }

  return true;
}

bool RewindSidebarWidget::navigate_sync_groups(ViewFrame* frame, uint64_t address) {
  if (!frame) {
    return false;
  }
  auto* file_context = frame->getFileContext();
  if (!file_context) {
    return false;
  }
  const auto& groups = file_context->allSyncGroups();
  for (auto* group : groups) {
    for (auto* member : group->members()) {
      View* group_view = member->getCurrentViewInterface();
      if (!group_view) {
        continue;
      }
      auto data = group_view->getData();
      if (data && data == m_data && group_view->getCurrentFunction()) {
        if (member->navigate(m_data, address, true, true)) {
          return true;
        }
      }
    }
  }
  return false;
}

bool RewindSidebarWidget::ensure_function_at(uint64_t address) {
  if (!m_data) {
    return false;
  }

  auto functions = m_data->GetAnalysisFunctionsContainingAddress(address);
  if (!functions.empty()) {
    return true;
  }

  auto segment = m_data->GetSegmentAt(address);
  if (!segment) {
    if (logger_) {
      logger_->LogWarn(
          "Rewind: no segment at 0x%llx; skip CreateUserFunction", static_cast<unsigned long long>(address)
      );
    }
    return false;
  }

  auto platform = m_data->GetDefaultPlatform();
  if (!platform) {
    if (logger_) {
      logger_->LogWarn(
          "Rewind: no platform for CreateUserFunction at 0x%llx", static_cast<unsigned long long>(address)
      );
    }
    return false;
  }

  if (logger_) {
    logger_->LogInfo("Rewind: creating user function at 0x%llx", static_cast<unsigned long long>(address));
  }
  auto id = m_data->BeginUndoActions();
  m_data->CreateUserFunction(platform, address);
  m_data->ForgetUndoActions(id);
  return true;
}

void RewindSidebarWidget::reset_trace_ui() {
  m_controls->set_trace_loaded(false);
  set_trace_path("No Trace Loaded");
  set_position("- / -");
  set_status("Idle");
  m_controls->set_controls_enabled(false);
  m_trace_loaded = false;
  m_controls_enabled = false;
  m_last_nav_address.reset();
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

void RewindSidebarWidget::update_position(const binja_rewind::ReplayUpdate& update) {
  if (update.has_position) {
    set_position(QString::number(update.sequence));
  } else if (update.trace_loaded) {
    set_position("- / -");
  }
}

void RewindSidebarWidget::update_registers(const std::vector<binja_rewind::RegisterValue>& registers) {
  if (!m_registers_view) {
    return;
  }
  m_registers_view->setRegisters(registers);
}

void RewindSidebarWidget::update_trace_slice(const binja_rewind::ReplayUpdate& update) {
  if (!m_trace_slice_view) {
    return;
  }
  if (!update.view_address.has_value()) {
    m_trace_slice_view->clear();
    return;
  }

  std::vector<TraceSliceEntry> entries;
  entries.reserve(update.past_addresses.size() + update.future_addresses.size() + 1);

  for (int i = static_cast<int>(update.past_addresses.size()) - 1; i >= 0; --i) {
    uint64_t addr = update.past_addresses[static_cast<size_t>(i)];
    TraceSliceEntry entry{};
    entry.relative = -(i + 1);
    entry.address = addr;
    entry.text = format_disasm(addr);
    entry.kind = TraceSliceEntry::Kind::Past;
    entries.push_back(std::move(entry));
  }

  {
    uint64_t addr = *update.view_address;
    TraceSliceEntry entry{};
    entry.relative = 0;
    entry.address = addr;
    entry.text = format_disasm(addr);
    entry.kind = TraceSliceEntry::Kind::Current;
    entries.push_back(std::move(entry));
  }

  for (size_t i = 0; i < update.future_addresses.size(); ++i) {
    uint64_t addr = update.future_addresses[i];
    TraceSliceEntry entry{};
    entry.relative = static_cast<int>(i + 1);
    entry.address = addr;
    entry.text = format_disasm(addr);
    entry.kind = TraceSliceEntry::Kind::Future;
    entries.push_back(std::move(entry));
  }

  m_trace_slice_view->setEntries(entries);
  if (!entries.empty()) {
    int current_row = static_cast<int>(update.past_addresses.size());
    m_trace_slice_view->selectRow(current_row);
  }
}

void RewindSidebarWidget::update_session_view(const binja_rewind::ReplayUpdate& update) {
  if (!m_session_view || !update.trace_info_changed) {
    return;
  }
  m_session_view->setSummary(update.summary);
  m_session_view->setModules(update.modules);
}

void RewindSidebarWidget::update_navigation(const binja_rewind::ReplayUpdate& update) {
  if (update.view_address.has_value()) {
    const uint64_t addr = *update.view_address;
    if (!m_last_nav_address.has_value() || *m_last_nav_address != addr) {
      if (navigate_to_address(addr)) {
        m_last_nav_address = addr;
      }
    }
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
  m_trace_loaded = update.trace_loaded;
  m_controls_enabled = update.controls_enabled;

  refresh_frontier_setting();

  update_threads(update.threads, update.thread_id);
  update_position(update);
  update_registers(update.registers);
  update_trace_slice(update);
  update_session_view(update);
  update_navigation(update);
}

bool RewindSidebarWidget::can_handle_action(const QString& action_name, const UIActionContext& context) const {
  if (!m_worker || !m_trace_loaded || !m_controls_enabled) {
    return false;
  }
  if (!context.binaryView) {
    return false;
  }
  if (m_data && context.binaryView != m_data) {
    return false;
  }
  if (action_name.isEmpty()) {
    return false;
  }
  return true;
}

bool RewindSidebarWidget::handle_action(const QString& action_name, const UIActionContext& context) {
  if (!can_handle_action(action_name, context)) {
    return false;
  }
  if (!m_worker) {
    return false;
  }

  auto action_id = rewind_ui::action_id_from_name(action_name);
  if (!action_id.has_value()) {
    return false;
  }

  switch (*action_id) {
  case rewind_ui::ActionId::Resume:
    m_worker->run_forward();
    return true;
  case rewind_ui::ActionId::GoBackwards:
    m_worker->run_backward();
    return true;
  case rewind_ui::ActionId::StepInto:
    m_worker->step_instruction();
    return true;
  case rewind_ui::ActionId::StepIntoBackwards:
    m_worker->step_instruction_backward();
    return true;
  case rewind_ui::ActionId::StepOver:
    m_worker->step_over();
    return true;
  case rewind_ui::ActionId::StepOverBackwards:
    m_worker->step_over_backward();
    return true;
  case rewind_ui::ActionId::StepReturn:
    m_worker->step_out();
    return true;
  case rewind_ui::ActionId::StepReturnBackwards:
    m_worker->step_out_backward();
    return true;
  case rewind_ui::ActionId::Pause:
    m_worker->pause();
    return true;
  case rewind_ui::ActionId::RunToHere:
    m_worker->run_to_view_address(context.address, true);
    return true;
  case rewind_ui::ActionId::RunBackToHere:
    m_worker->run_to_view_address(context.address, false);
    return true;
  case rewind_ui::ActionId::DefineFunctionsFromTrace:
    m_worker->define_functions_from_trace();
    return true;
  }
  return false;
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
