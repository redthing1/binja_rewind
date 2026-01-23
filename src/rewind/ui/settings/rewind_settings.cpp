#include "rewind/ui/settings/rewind_settings.hpp"

namespace {

constexpr const char* kSettingsGroup = "rewind";
constexpr const char* kFrontierKey = "rewind.frontierSize";
constexpr const char* kReverseHistoryKey = "rewind.reverseHistorySize";
constexpr int64_t kFrontierMin = 1;
constexpr int64_t kFrontierMax = 64;
constexpr size_t kDefaultFrontier = 8;
constexpr int64_t kReverseHistoryMin = 1024;
constexpr int64_t kReverseHistoryMax = 1048576;
constexpr size_t kDefaultReverseHistory = 65536;

} // namespace

namespace binja::rewind::ui {

void register_settings() {
  auto settings = BinaryNinja::Settings::Instance();
  settings->RegisterGroup(kSettingsGroup, "Rewind");
  settings->RegisterSetting(
      kFrontierKey,
      R"({
      "title" : "Temporal Gradient Size",
      "type" : "number",
      "default" : 8,
      "description" : "Number of steps to highlight before and after the current instruction.",
      "min" : 1,
      "max" : 64
    })"
  );
  settings->RegisterSetting(
      kReverseHistoryKey,
      R"({
      "title" : "Reverse Playback Window Size",
      "type" : "number",
      "default" : 65536,
      "description" : "Number of flow steps cached for reverse playback; larger values improve reverse speed.",
      "min" : 1024,
      "max" : 1048576
    })"
  );
}

size_t get_frontier_size(const BinaryViewRef& view) {
  if (!view) {
    return kDefaultFrontier;
  }
  auto settings = BinaryNinja::Settings::Instance();
  int64_t value = settings->Get<int64_t>(kFrontierKey, view);
  if (value < kFrontierMin) {
    value = kFrontierMin;
  } else if (value > kFrontierMax) {
    value = kFrontierMax;
  }
  return static_cast<size_t>(value);
}

size_t get_reverse_history_size(const BinaryViewRef& view) {
  if (!view) {
    return kDefaultReverseHistory;
  }
  auto settings = BinaryNinja::Settings::Instance();
  int64_t value = settings->Get<int64_t>(kReverseHistoryKey, view);
  if (value < kReverseHistoryMin) {
    value = kReverseHistoryMin;
  } else if (value > kReverseHistoryMax) {
    value = kReverseHistoryMax;
  }
  return static_cast<size_t>(value);
}

} // namespace binja::rewind::ui
