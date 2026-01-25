#include "rewind/ui/controllers/sidebar_controller.hpp"

#include <QFileDialog>
#include <QPointer>
#include <QString>

#include "filecontext.h"
#include "rewind/core/worker/rewind_worker.hpp"
#include "rewind/ui/actions/rewind_actions.hpp"
#include "rewind/ui/settings/rewind_settings.hpp"
#include "syncgroup.h"
#include "uicontext.h"
#include "viewframe.h"

namespace binja::rewind::ui {

SidebarController::SidebarController(SidebarView& view, BinaryViewRef data, ViewFrame* frame)
    : view_(view), data_(data), frame_(frame) {
  if (data_) {
    logger_ = data_->CreateLogger("Rewind");
  } else {
    logger_ = BinaryNinja::LogRegistry::CreateLogger("Rewind");
  }
  setup_worker();
  refresh_frontier_setting();
  refresh_reverse_history_setting();
}

SidebarController::~SidebarController() = default;

void SidebarController::setup_worker() {
  gradient_ = std::make_unique<GradientPainter>(frontier_size_);
  QPointer<QWidget> guard(view_.widget());
  worker_ = std::make_unique<core::RewindWorker>(data_, [this, guard](const core::model::ReplayUpdate& update) {
    if (!guard) {
      return;
    }
    apply_update(update);
  });
}

void SidebarController::refresh_frontier_setting() {
  size_t frontier = binja::rewind::ui::get_frontier_size(data_);
  if (frontier == frontier_size_) {
    return;
  }
  frontier_size_ = frontier;
  if (gradient_) {
    gradient_->set_frontier_size(frontier);
  }
  if (worker_) {
    worker_->set_gradient_size(frontier);
  }
}

void SidebarController::refresh_reverse_history_setting() {
  size_t size = binja::rewind::ui::get_reverse_history_size(data_);
  if (size == reverse_history_size_) {
    return;
  }
  reverse_history_size_ = size;
  if (worker_) {
    worker_->set_reverse_history_size(size);
  }
}

void SidebarController::notify_font_changed() { view_.refresh_viewports(); }

void SidebarController::notify_theme_changed() {
  refresh_frontier_setting();
  refresh_reverse_history_setting();
  view_.refresh_viewports();
}

void SidebarController::on_load_trace_requested() {
  const QString path = QFileDialog::getOpenFileName(
      view_.widget(), "Open w1rewind trace", QString(), "Trace Files (*.w1rwnd *.w1r);;All Files (*)"
  );
  if (path.isEmpty()) {
    return;
  }

  trace_path_value_ = path.toStdString();
  if (worker_) {
    worker_->load_trace(trace_path_value_);
  }
}

void SidebarController::on_clear_trace_requested() {
  if (worker_) {
    worker_->clear_trace();
  }
}

void SidebarController::on_thread_selected(uint64_t thread_id) {
  if (!worker_ || thread_id == 0) {
    return;
  }
  worker_->select_thread(thread_id);
}

void SidebarController::on_trace_slice_double_clicked(int row) {
  if (row < 0 || static_cast<size_t>(row) >= trace_entries_.size()) {
    return;
  }
  navigate_to_address(trace_entries_[static_cast<size_t>(row)].address);
}

void SidebarController::run_to_start() {
  if (worker_) {
    worker_->run_to_start();
  }
}

void SidebarController::run_forward() {
  if (worker_) {
    worker_->run_forward();
  }
}

void SidebarController::run_backward() {
  if (worker_) {
    worker_->run_backward();
  }
}

void SidebarController::pause() {
  if (worker_) {
    worker_->pause();
  }
}

void SidebarController::step_instruction() {
  if (worker_) {
    worker_->step_instruction();
  }
}

void SidebarController::step_instruction_backward() {
  if (worker_) {
    worker_->step_instruction_backward();
  }
}

void SidebarController::step_over() {
  if (worker_) {
    worker_->step_over();
  }
}

void SidebarController::step_out() {
  if (worker_) {
    worker_->step_out();
  }
}

bool SidebarController::can_handle_action(const QString& action_name, const UIActionContext& context) const {
  if (!worker_ || !trace_loaded_ || !controls_enabled_) {
    return false;
  }
  if (!context.binaryView) {
    return false;
  }
  if (data_ && context.binaryView != data_) {
    return false;
  }
  if (action_name.isEmpty()) {
    return false;
  }
  return true;
}

bool SidebarController::handle_action(const QString& action_name, const UIActionContext& context) {
  if (!can_handle_action(action_name, context)) {
    return false;
  }
  if (!worker_) {
    return false;
  }

  auto action_id = binja::rewind::ui::action_id_from_name(action_name);
  if (!action_id.has_value()) {
    return false;
  }

  switch (*action_id) {
  case binja::rewind::ui::ActionId::Resume:
    worker_->run_forward();
    return true;
  case binja::rewind::ui::ActionId::GoBackwards:
    worker_->run_backward();
    return true;
  case binja::rewind::ui::ActionId::StepInto:
    worker_->step_instruction();
    return true;
  case binja::rewind::ui::ActionId::StepIntoBackwards:
    worker_->step_instruction_backward();
    return true;
  case binja::rewind::ui::ActionId::StepOver:
    worker_->step_over();
    return true;
  case binja::rewind::ui::ActionId::StepOverBackwards:
    worker_->step_over_backward();
    return true;
  case binja::rewind::ui::ActionId::StepReturn:
    worker_->step_out();
    return true;
  case binja::rewind::ui::ActionId::StepReturnBackwards:
    worker_->step_out_backward();
    return true;
  case binja::rewind::ui::ActionId::Pause:
    worker_->pause();
    return true;
  case binja::rewind::ui::ActionId::RunToHere:
    worker_->run_to_view_address(context.address, true);
    return true;
  case binja::rewind::ui::ActionId::RunBackToHere:
    worker_->run_to_view_address(context.address, false);
    return true;
  case binja::rewind::ui::ActionId::FunctionDiscoveryAnalysis:
    worker_->run_function_discovery_analysis();
    return true;
  case binja::rewind::ui::ActionId::ControlFlowEdgeAnalysis:
    worker_->run_control_flow_edge_analysis();
    return true;
  }
  return false;
}

void SidebarController::apply_update(const core::model::ReplayUpdate& update) {
  if (!update.status.empty()) {
    view_.set_status(QString::fromStdString(update.status));
  }

  if (update.status_only) {
    return;
  }

  if (update.trace_cleared) {
    view_.reset_trace_ui();
    trace_loaded_ = false;
    controls_enabled_ = false;
    last_nav_address_.reset();
    trace_entries_.clear();
    if (gradient_) {
      gradient_->clear(data_);
    }
    return;
  }

  if (update.trace_loaded) {
    view_.set_trace_loaded(true);
    view_.set_trace_path(QString::fromStdString(update.trace_path));
  }

  view_.set_controls_enabled(update.controls_enabled);
  trace_loaded_ = update.trace_loaded;
  controls_enabled_ = update.controls_enabled;

  refresh_frontier_setting();
  refresh_reverse_history_setting();

  update_threads(update.threads, update.thread_id);
  update_position(update);
  update_registers(update.registers);
  update_trace_slice(update);
  update_session_view(update);
  update_navigation(update);
}

void SidebarController::update_threads(const std::vector<core::model::ThreadInfo>& threads, uint64_t current_thread) {
  view_.set_thread_list(threads, current_thread);
}

void SidebarController::update_position(const core::model::ReplayUpdate& update) {
  if (update.has_position) {
    view_.set_position(QString::number(update.sequence));
  } else if (update.trace_loaded) {
    view_.set_position("- / -");
  }
}

void SidebarController::update_registers(const std::vector<core::model::RegisterValue>& registers) {
  view_.set_registers(registers);
}

void SidebarController::update_trace_slice(const core::model::ReplayUpdate& update) {
  trace_entries_.clear();
  if (!update.view_address.has_value()) {
    view_.set_trace_entries(trace_entries_, -1);
    return;
  }

  trace_entries_.reserve(update.past_addresses.size() + update.future_addresses.size() + 1);

  for (int i = static_cast<int>(update.past_addresses.size()) - 1; i >= 0; --i) {
    uint64_t addr = update.past_addresses[static_cast<size_t>(i)];
    TraceSliceEntry entry{};
    entry.relative = -(i + 1);
    entry.address = addr;
    entry.text = format_disasm(addr);
    entry.kind = TraceSliceEntry::Kind::Past;
    trace_entries_.push_back(std::move(entry));
  }

  {
    uint64_t addr = *update.view_address;
    TraceSliceEntry entry{};
    entry.relative = 0;
    entry.address = addr;
    entry.text = format_disasm(addr);
    entry.kind = TraceSliceEntry::Kind::Current;
    trace_entries_.push_back(std::move(entry));
  }

  for (size_t i = 0; i < update.future_addresses.size(); ++i) {
    uint64_t addr = update.future_addresses[i];
    TraceSliceEntry entry{};
    entry.relative = static_cast<int>(i + 1);
    entry.address = addr;
    entry.text = format_disasm(addr);
    entry.kind = TraceSliceEntry::Kind::Future;
    trace_entries_.push_back(std::move(entry));
  }

  int current_row = static_cast<int>(update.past_addresses.size());
  view_.set_trace_entries(trace_entries_, current_row);
}

void SidebarController::update_session_view(const core::model::ReplayUpdate& update) {
  if (!update.trace_info_changed) {
    return;
  }
  view_.set_session_summary(update.summary);
  view_.set_session_modules(update.modules);
}

void SidebarController::update_navigation(const core::model::ReplayUpdate& update) {
  if (update.view_address.has_value()) {
    const uint64_t addr = *update.view_address;
    if (!last_nav_address_.has_value() || *last_nav_address_ != addr) {
      if (navigate_to_address(addr)) {
        last_nav_address_ = addr;
      }
    }
  }

  if (gradient_) {
    if (update.view_address.has_value()) {
      gradient_->paint(data_, update.view_address, update.past_addresses, update.future_addresses);
    } else {
      gradient_->clear(data_);
    }
  }
}

QString SidebarController::format_disasm(uint64_t address) const {
  if (!data_) {
    return QString("0x%1").arg(address, 0, 16);
  }
  auto arch = data_->GetDefaultArchitecture();
  if (!arch) {
    return QString("0x%1").arg(address, 0, 16);
  }

  size_t max_len = arch->GetMaxInstructionLength();
  if (max_len == 0) {
    max_len = 16;
  }
  std::vector<uint8_t> bytes(max_len);
  size_t read = data_->Read(bytes.data(), address, max_len);
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

bool SidebarController::navigate_to_address(uint64_t address) {
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
    navigated = frame->navigate(data_, address, true, true);
  } else {
    navigated = navigate_sync_groups(frame, address);
  }

  if (!navigated) {
    navigated = frame->navigate(data_, address, true, true);
  }

  if (context) {
    context->refreshCurrentViewContents();
  }

  return navigated;
}

bool SidebarController::resolve_navigation_context(UIContext*& context, ViewFrame*& frame, View*& view) {
  context = UIContext::contextForWidget(view_.widget());
  frame = frame_ ? frame_ : (context ? context->getCurrentViewFrame() : nullptr);
  if (!frame) {
    return false;
  }

  view = frame->getCurrentViewInterface();
  if (!view) {
    return false;
  }

  return true;
}

bool SidebarController::navigate_sync_groups(ViewFrame* frame, uint64_t address) {
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
      if (data && data == data_ && group_view->getCurrentFunction()) {
        if (member->navigate(data_, address, true, true)) {
          return true;
        }
      }
    }
  }
  return false;
}

bool SidebarController::ensure_function_at(uint64_t address) {
  if (!data_) {
    return false;
  }

  auto functions = data_->GetAnalysisFunctionsContainingAddress(address);
  if (!functions.empty()) {
    return true;
  }

  auto segment = data_->GetSegmentAt(address);
  if (!segment) {
    if (logger_) {
      logger_->LogWarn(
          "Rewind: no segment at 0x%llx; skip CreateUserFunction", static_cast<unsigned long long>(address)
      );
    }
    return false;
  }

  auto platform = data_->GetDefaultPlatform();
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
  auto id = data_->BeginUndoActions();
  data_->CreateUserFunction(platform, address);
  data_->ForgetUndoActions(id);
  return true;
}

} // namespace binja::rewind::ui
