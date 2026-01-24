#include "rewind/core/worker/rewind_worker.hpp"

#include <sstream>
#include <unordered_set>
#include <utility>

namespace binja::rewind::core {

RewindWorker::RewindWorker(BinaryNinja::Ref<BinaryNinja::BinaryView> view, UpdateCallback callback)
    : engine_(std::move(view)), callback_(std::move(callback)) {
  worker_ = std::thread([this]() { worker_loop(); });
}

RewindWorker::~RewindWorker() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  engine_.request_cancel();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void RewindWorker::enqueue(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) {
      return;
    }
    queue_.push_back(std::move(task));
  }
  cv_.notify_one();
}

void RewindWorker::worker_loop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
      if (stop_) {
        return;
      }
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    if (task) {
      task();
    }
  }
}

void RewindWorker::post_update(model::ReplayUpdate update) {
  if (!callback_) {
    return;
  }
  BinaryNinja::ExecuteOnMainThread([cb = callback_, update = std::move(update)]() mutable { cb(update); });
}

void RewindWorker::load_trace(const std::string& path) {
  enqueue([this, path]() { post_update(engine_.load_trace(path)); });
}

void RewindWorker::clear_trace() {
  enqueue([this]() { post_update(engine_.clear_trace()); });
}

void RewindWorker::select_thread(uint64_t thread_id) {
  enqueue([this, thread_id]() { post_update(engine_.select_thread(thread_id)); });
}

void RewindWorker::step_instruction() {
  enqueue([this]() { post_update(engine_.step_instruction(true)); });
}

void RewindWorker::step_instruction_backward() {
  enqueue([this]() { post_update(engine_.step_instruction(false)); });
}

void RewindWorker::step_flow() {
  enqueue([this]() { post_update(engine_.step_flow(true)); });
}

void RewindWorker::step_flow_backward() {
  enqueue([this]() { post_update(engine_.step_flow(false)); });
}

void RewindWorker::step_over() {
  enqueue([this]() { post_update(engine_.step_over()); });
}

void RewindWorker::step_out() {
  enqueue([this]() { post_update(engine_.step_out()); });
}

void RewindWorker::step_over_backward() {
  enqueue([this]() { post_update(engine_.step_over_backward()); });
}

void RewindWorker::step_out_backward() {
  enqueue([this]() { post_update(engine_.step_out_backward()); });
}

void RewindWorker::run_forward() {
  enqueue([this]() {
    model::ReplayUpdate seeking{};
    seeking.status = "Seeking forward...";
    seeking.status_only = true;
    post_update(std::move(seeking));
    std::unordered_set<uint64_t> breakpoints;
    BinaryNinja::ExecuteOnMainThreadAndWait([&]() { breakpoints = engine_.collect_breakpoints(); });
    post_update(engine_.run_forward(breakpoints));
  });
}

void RewindWorker::run_backward() {
  enqueue([this]() {
    model::ReplayUpdate seeking{};
    seeking.status = "Seeking backward...";
    seeking.status_only = true;
    post_update(std::move(seeking));
    std::unordered_set<uint64_t> breakpoints;
    BinaryNinja::ExecuteOnMainThreadAndWait([&]() { breakpoints = engine_.collect_breakpoints(); });
    post_update(engine_.run_backward(breakpoints));
  });
}

void RewindWorker::run_to_address(uint64_t trace_address, bool forward) {
  enqueue([this, trace_address, forward]() {
    auto to_hex = [](uint64_t value) {
      std::ostringstream oss;
      oss << std::hex << value;
      return oss.str();
    };
    model::ReplayUpdate seeking{};
    seeking.status = forward ? ("Seeking to 0x" + to_hex(trace_address) + "...")
                             : ("Seeking back to 0x" + to_hex(trace_address) + "...");
    seeking.status_only = true;
    post_update(std::move(seeking));
    post_update(engine_.run_to_address(trace_address, forward));
  });
}

void RewindWorker::run_to_view_address(uint64_t view_address, bool forward) {
  enqueue([this, view_address, forward]() {
    auto to_hex = [](uint64_t value) {
      std::ostringstream oss;
      oss << std::hex << value;
      return oss.str();
    };
    model::ReplayUpdate seeking{};
    seeking.status = forward ? ("Seeking to view 0x" + to_hex(view_address) + "...")
                             : ("Seeking back to view 0x" + to_hex(view_address) + "...");
    seeking.status_only = true;
    post_update(std::move(seeking));
    post_update(engine_.run_to_view_address(view_address, forward));
  });
}

void RewindWorker::run_to_start() {
  enqueue([this]() {
    model::ReplayUpdate seeking{};
    seeking.status = "Rewinding to start...";
    seeking.status_only = true;
    post_update(std::move(seeking));
    post_update(engine_.run_to_start());
  });
}

void RewindWorker::pause() {
  engine_.request_cancel();
  model::ReplayUpdate update{};
  update.status = "Paused";
  update.status_only = true;
  post_update(std::move(update));
}

void RewindWorker::set_gradient_size(size_t size) {
  enqueue([this, size]() { engine_.set_gradient_size(size); });
}

void RewindWorker::set_reverse_history_size(size_t size) {
  enqueue([this, size]() { engine_.set_reverse_history_size(size); });
}

void RewindWorker::define_functions_from_trace() {
  enqueue([this]() {
    auto final_update =
        engine_.define_functions_from_trace([this](model::ReplayUpdate update) { post_update(std::move(update)); });
    post_update(std::move(final_update));
  });
}

} // namespace binja::rewind::core
