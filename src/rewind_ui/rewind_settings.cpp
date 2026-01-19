#include "rewind_ui/rewind_settings.hpp"

namespace {

constexpr const char* kSettingsGroup = "rewind";
constexpr const char* kFrontierKey = "rewind.frontierSize";
constexpr int64_t kFrontierMin = 1;
constexpr int64_t kFrontierMax = 64;
constexpr size_t kDefaultFrontier = 8;

} // namespace

namespace rewind_ui {

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

} // namespace rewind_ui
