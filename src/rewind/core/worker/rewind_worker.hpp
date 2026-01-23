#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "binaryninjaapi.h"
#include "rewind/core/engine/rewind_engine.hpp"

namespace binja::rewind::core {

class RewindWorker {
public:
  using UpdateCallback = std::function<void(const model::ReplayUpdate&)>;

  explicit RewindWorker(BinaryNinja::Ref<BinaryNinja::BinaryView> view, UpdateCallback callback);
  ~RewindWorker();

  void load_trace(const std::string& path);
  void clear_trace();
  void select_thread(uint64_t thread_id);

  void step_instruction();
  void step_instruction_backward();
  void step_flow();
  void step_flow_backward();
  void step_over();
  void step_out();
  void step_over_backward();
  void step_out_backward();

  void run_forward();
  void run_backward();
  void run_to_address(uint64_t trace_address, bool forward);
  void run_to_view_address(uint64_t view_address, bool forward);
  void run_to_start();
  void pause();
  void set_gradient_size(size_t size);
  void set_reverse_history_size(size_t size);
  void define_functions_from_trace();

private:
  void enqueue(std::function<void()> task);
  void worker_loop();
  void post_update(model::ReplayUpdate update);
  engine::RewindEngine engine_;
  UpdateCallback callback_;
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> queue_;
  bool stop_ = false;
};

} // namespace binja::rewind::core
