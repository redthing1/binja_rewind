#include "rewind_ui/session_view.hpp"

#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QStringList>
#include <QVBoxLayout>
#include <algorithm>

#include "fontsettings.h"
#include "theme.h"

namespace {

QString permission_string(uint32_t perm) {
  QString out;
  out += (perm & 1u) ? 'r' : '-';
  out += (perm & 2u) ? 'w' : '-';
  out += (perm & 4u) ? 'x' : '-';
  return out;
}

QString basename(const QString& path) {
  if (path.isEmpty()) {
    return QString();
  }
  QString trimmed = path;
  while (trimmed.endsWith('/') || trimmed.endsWith('\\')) {
    trimmed.chop(1);
  }
  int idx = trimmed.lastIndexOf('/');
  int idx2 = trimmed.lastIndexOf('\\');
  int pos = std::max(idx, idx2);
  if (pos >= 0) {
    return trimmed.mid(pos + 1);
  }
  return trimmed;
}

} // namespace

ModulesModel::ModulesModel(QObject* parent) : QAbstractTableModel(parent) {}

int ModulesModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(m_modules.size());
}

int ModulesModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return 4;
}

QVariant ModulesModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }
  if (index.row() < 0 || static_cast<size_t>(index.row()) >= m_modules.size()) {
    return QVariant();
  }

  const auto& module = m_modules[static_cast<size_t>(index.row())];

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0:
      return basename(QString::fromStdString(module.path));
    case 1:
      return QString("0x%1").arg(module.base, 0, 16);
    case 2:
      return QString("0x%1").arg(module.size, 0, 16);
    case 3:
      return permission_string(module.permissions);
    default:
      break;
    }
  }

  if (role == Qt::ToolTipRole && index.column() == 0) {
    return QString::fromStdString(module.path);
  }

  if (role == Qt::TextAlignmentRole && (index.column() == 1 || index.column() == 2)) {
    return QVariant::fromValue(int(Qt::AlignRight | Qt::AlignVCenter));
  }

  return QVariant();
}

QVariant ModulesModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (role != Qt::DisplayRole) {
    return QVariant();
  }
  if (orientation == Qt::Vertical) {
    return QVariant();
  }
  switch (section) {
  case 0:
    return QStringLiteral("Module");
  case 1:
    return QStringLiteral("Base");
  case 2:
    return QStringLiteral("Size");
  case 3:
    return QStringLiteral("Perm");
  default:
    return QVariant();
  }
}

void ModulesModel::setModules(const std::vector<binja_rewind::TraceModule>& modules) {
  beginResetModel();
  m_modules = modules;
  endResetModel();
}

void ModulesModel::clear() {
  beginResetModel();
  m_modules.clear();
  endResetModel();
}

SessionView::SessionView(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  auto* summary = new QWidget(this);
  auto* summary_layout = new QFormLayout(summary);
  summary_layout->setContentsMargins(0, 0, 0, 0);
  summary_layout->setHorizontalSpacing(10);
  summary_layout->setVerticalSpacing(4);

  m_arch = make_value_label();
  m_target = make_value_label();
  m_version = make_value_label();
  m_threads = make_value_label();
  m_modules = make_value_label();
  m_features = make_value_label();

  summary_layout->addRow("Architecture", m_arch);
  summary_layout->addRow("Target", m_target);
  summary_layout->addRow("Trace", m_version);
  summary_layout->addRow("Threads", m_threads);
  summary_layout->addRow("Modules", m_modules);
  summary_layout->addRow("Features", m_features);

  auto* modules_label = new QLabel("Modules", this);
  modules_label->setStyleSheet("font-weight: 600;");

  m_modules_model = new ModulesModel(this);
  m_modules_table = new QTableView(this);
  m_modules_table->setModel(m_modules_model);
  m_modules_table->setSelectionMode(QAbstractItemView::NoSelection);
  m_modules_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_modules_table->setShowGrid(false);
  m_modules_table->setAlternatingRowColors(false);
  m_modules_table->verticalHeader()->setVisible(false);
  m_modules_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_modules_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_modules_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_modules_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  m_modules_table->setFocusPolicy(Qt::NoFocus);

  layout->addWidget(summary);
  layout->addWidget(modules_label);
  layout->addWidget(m_modules_table, 1);
}

QLabel* SessionView::make_value_label() {
  auto* label = new QLabel(this);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  return label;
}

void SessionView::set_label(QLabel* label, const QString& value) {
  if (!label) {
    return;
  }
  label->setText(value.isEmpty() ? QStringLiteral("-") : value);
}

void SessionView::setSummary(const binja_rewind::TraceSummary& summary) {
  QString arch = summary.arch.empty() ? QStringLiteral("Unknown") : QString::fromStdString(summary.arch);
  QString target;
  if (!summary.os.empty() || !summary.abi.empty() || !summary.cpu.empty()) {
    QStringList parts;
    if (!summary.os.empty()) {
      parts << QString::fromStdString(summary.os);
    }
    if (!summary.abi.empty()) {
      parts << QString::fromStdString(summary.abi);
    }
    if (!summary.cpu.empty()) {
      parts << QString::fromStdString(summary.cpu);
    }
    target = parts.join(" / ");
  }

  QString features;
  QStringList feats;
  if (summary.has_blocks) {
    feats << "Blocks";
  }
  if (summary.has_registers) {
    feats << "Registers";
  }
  if (summary.has_memory_access) {
    feats << "Mem Access";
  }
  if (summary.has_memory_values) {
    feats << "Mem Values";
  }
  if (summary.has_stack_snapshot) {
    feats << "Stack Snap";
  }
  features = feats.join(", ");

  set_label(m_arch, arch);
  set_label(m_target, target);
  set_label(m_version, QString("v%1").arg(summary.trace_version));
  set_label(m_threads, QString::number(summary.thread_count));
  set_label(m_modules, QString::number(summary.module_count));
  set_label(m_features, features);
}

void SessionView::setModules(const std::vector<binja_rewind::TraceModule>& modules) {
  m_modules_model->setModules(modules);
}

void SessionView::clear() {
  set_label(m_arch, QString());
  set_label(m_target, QString());
  set_label(m_version, QString());
  set_label(m_threads, QString());
  set_label(m_modules, QString());
  set_label(m_features, QString());
  m_modules_model->clear();
}
