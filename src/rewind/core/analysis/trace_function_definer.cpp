#include "rewind/core/analysis/trace_function_definer.hpp"

#include <algorithm>
#include <unordered_set>

#include "w1rewind/replay/flow_cursor.hpp"
#include "w1rewind/trace/trace_reader.hpp"

namespace binja::rewind::core::analysis {

DefineFunctionsResult TraceFunctionDefiner::define_functions(
    const w1::rewind::replay_session& session, const std::shared_ptr<w1::rewind::trace_index>& index,
    const mapping::AddressMapper& mapper, const BinaryNinja::Ref<BinaryNinja::BinaryView>& view,
    const std::string& trace_path, const BinaryNinja::Ref<BinaryNinja::Logger>& logger
) const {
  DefineFunctionsResult result{};

  if (!index) {
    result.error = "trace index unavailable";
    return result;
  }

  auto stream = std::make_shared<w1::rewind::trace_reader>(trace_path);

  w1::rewind::record_stream_cursor stream_cursor(stream);
  w1::rewind::flow_extractor extractor(&session.context());
  w1::rewind::history_window history(1);
  w1::rewind::flow_cursor cursor(std::move(stream_cursor), std::move(extractor), std::move(history), index);
  if (!cursor.open()) {
    result.error = std::string(cursor.error());
    return result;
  }

  const auto& threads = session.threads();
  if (threads.empty()) {
    result.error = "trace has no threads";
    return result;
  }

  std::unordered_set<uint64_t> addresses;
  for (const auto& thread : threads) {
    if (!cursor.seek(thread.thread_id, 0)) {
      if (logger) {
        std::string cursor_error(cursor.error());
        logger->LogWarn(
            "Rewind: scan failed to seek thread %llu: %s", static_cast<unsigned long long>(thread.thread_id),
            cursor_error.c_str()
        );
      }
      continue;
    }

    w1::rewind::flow_step step{};
    while (cursor.step_forward(step)) {
      ++result.steps_scanned;
      auto view_address = mapper.trace_to_view(step.address, step.size ? step.size : 1);
      if (view_address.has_value()) {
        addresses.insert(*view_address);
      }
    }

    auto kind = cursor.error_kind();
    if (kind != w1::rewind::flow_error_kind::end_of_trace) {
      if (logger) {
        std::string cursor_error(cursor.error());
        logger->LogWarn(
            "Rewind: scan halted on thread %llu: %s", static_cast<unsigned long long>(thread.thread_id),
            cursor_error.c_str()
        );
      }
    }
  }

  if (addresses.empty()) {
    result.ok = true;
    return result;
  }

  std::vector<uint64_t> candidates(addresses.begin(), addresses.end());
  std::sort(candidates.begin(), candidates.end());
  result.candidates = candidates.size();

  bool missing_platform = false;

  BinaryNinja::ExecuteOnMainThreadAndWait([&]() {
    if (!view) {
      result.error = "binary view unavailable";
      return;
    }
    auto platform = view->GetDefaultPlatform();
    if (!platform) {
      missing_platform = true;
      return;
    }

    auto undo = view->BeginUndoActions();
    for (uint64_t address : candidates) {
      if (!view->GetSegmentAt(address)) {
        ++result.no_segment;
        continue;
      }
      auto funcs = view->GetAnalysisFunctionsContainingAddress(address);
      if (!funcs.empty()) {
        ++result.skipped;
        continue;
      }
      view->CreateUserFunction(platform, address);
      ++result.created;
    }
    view->ForgetUndoActions(undo);
  });

  if (missing_platform) {
    result.error = "no platform available to create functions";
    return result;
  }
  if (!result.error.empty()) {
    return result;
  }

  result.ok = true;
  return result;
}

} // namespace binja::rewind::core::analysis
