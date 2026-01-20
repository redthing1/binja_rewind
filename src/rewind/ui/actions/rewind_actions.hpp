#pragma once

#include <array>
#include <optional>

#include <QKeySequence>
#include <QString>

namespace binja::rewind::ui {

enum class ActionId {
  Resume,
  GoBackwards,
  StepInto,
  StepIntoBackwards,
  StepOver,
  StepOverBackwards,
  StepReturn,
  StepReturnBackwards,
  RunToHere,
  RunBackToHere,
  Pause,
  DefineFunctionsFromTrace,
};

inline constexpr const char* kDefineFunctionsFromTraceAction = "Define Functions from Trace";
inline constexpr const char* kDefineFunctionsFromTraceCommand = "Rewind\\Define Functions from Trace";

struct ActionSpec {
  ActionId id;
  const char* name;
  QKeySequence key;
};

inline const std::array<ActionSpec, 12>& action_specs() {
  static const std::array<ActionSpec, 12> specs = {
      ActionSpec{ActionId::Resume, "Resume", QKeySequence(Qt::Key_F9)},
      ActionSpec{ActionId::GoBackwards, "Go Backwards", QKeySequence(Qt::ShiftModifier | Qt::Key_F9)},
      ActionSpec{ActionId::StepInto, "Step Into", QKeySequence(Qt::Key_F7)},
      ActionSpec{ActionId::StepIntoBackwards, "Step Into Backwards", QKeySequence(Qt::ShiftModifier | Qt::Key_F7)},
      ActionSpec{ActionId::StepOver, "Step Over", QKeySequence(Qt::Key_F8)},
      ActionSpec{ActionId::StepOverBackwards, "Step Over Backwards", QKeySequence(Qt::ShiftModifier | Qt::Key_F8)},
      ActionSpec{ActionId::StepReturn, "Step Return", QKeySequence(Qt::ControlModifier | Qt::Key_F9)},
      ActionSpec{
          ActionId::StepReturnBackwards, "Step Return Backwards",
          QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_F9)
      },
      ActionSpec{ActionId::RunToHere, "Run To Here", QKeySequence(Qt::Key_F4)},
      ActionSpec{ActionId::RunBackToHere, "Run Back To Here", QKeySequence(Qt::ShiftModifier | Qt::Key_F4)},
      ActionSpec{ActionId::Pause, "Pause", QKeySequence(Qt::Key_F12)},
      ActionSpec{ActionId::DefineFunctionsFromTrace, kDefineFunctionsFromTraceAction, QKeySequence()},
  };
  return specs;
}

inline const ActionSpec* action_spec(ActionId id) {
  for (const auto& spec : action_specs()) {
    if (spec.id == id) {
      return &spec;
    }
  }
  return nullptr;
}

inline std::optional<ActionId> action_id_from_name(const QString& name) {
  for (const auto& spec : action_specs()) {
    if (name == spec.name) {
      return spec.id;
    }
  }
  return std::nullopt;
}

inline QString action_name(ActionId id) {
  if (const auto* spec = action_spec(id)) {
    return QString::fromUtf8(spec->name);
  }
  return {};
}

inline bool is_reverse_action(ActionId id) {
  switch (id) {
  case ActionId::GoBackwards:
  case ActionId::StepIntoBackwards:
  case ActionId::StepOverBackwards:
  case ActionId::StepReturnBackwards:
  case ActionId::RunBackToHere:
    return true;
  default:
    return false;
  }
}

} // namespace binja::rewind::ui
