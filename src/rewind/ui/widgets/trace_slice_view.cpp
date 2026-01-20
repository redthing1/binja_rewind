#include "rewind/ui/widgets/trace_slice_view.hpp"

#include <QHeaderView>
#include <QPainter>
#include <algorithm>

#include "fontsettings.h"
#include "theme.h"

namespace binja::rewind::ui {

TraceSliceModel::TraceSliceModel(QObject* parent) : QAbstractTableModel(parent) {}

int TraceSliceModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(m_entries.size());
}

int TraceSliceModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return 2;
}

QVariant TraceSliceModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }
  if (index.row() < 0 || static_cast<size_t>(index.row()) >= m_entries.size()) {
    return QVariant();
  }

  const auto& entry = m_entries[static_cast<size_t>(index.row())];

  if (role == Qt::DisplayRole) {
    if (index.column() == 0) {
      if (entry.relative == 0) {
        return QStringLiteral("0");
      }
      return QString(entry.relative > 0 ? "+%1" : "%1").arg(entry.relative);
    }
    if (index.column() == 1) {
      return entry.text;
    }
  }

  if (role == KindRole) {
    return static_cast<int>(entry.kind);
  }

  if (role == RelativeRole) {
    return entry.relative;
  }

  if (role == Qt::TextAlignmentRole && index.column() == 0) {
    return QVariant::fromValue(int(Qt::AlignRight | Qt::AlignVCenter));
  }

  return QVariant();
}

QVariant TraceSliceModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (role != Qt::DisplayRole) {
    return QVariant();
  }
  if (orientation == Qt::Vertical) {
    return QVariant();
  }
  if (section == 0) {
    return QStringLiteral("Δ");
  }
  if (section == 1) {
    return QStringLiteral("Instruction");
  }
  return QVariant();
}

void TraceSliceModel::setEntries(const std::vector<TraceSliceEntry>& entries) {
  beginResetModel();
  m_entries = entries;
  endResetModel();
}

void TraceSliceModel::clear() {
  beginResetModel();
  m_entries.clear();
  endResetModel();
}

const TraceSliceEntry* TraceSliceModel::entryAt(int row) const {
  if (row < 0 || static_cast<size_t>(row) >= m_entries.size()) {
    return nullptr;
  }
  return &m_entries[static_cast<size_t>(row)];
}

TraceSliceDelegate::TraceSliceDelegate(QObject* parent) : QStyledItemDelegate(parent) {
  m_mono = getMonospaceFont(dynamic_cast<QWidget*>(parent));
  QFontMetrics metrics(m_mono);
  m_row_height = metrics.height() + 6;
}

void TraceSliceDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  QStyleOptionViewItem opt(option);
  initStyleOption(&opt, index);

  painter->save();
  painter->setClipRect(opt.rect);
  painter->fillRect(opt.rect, opt.backgroundBrush);

  auto kind_value = index.data(TraceSliceModel::KindRole).toInt();
  TraceSliceEntry::Kind kind = static_cast<TraceSliceEntry::Kind>(kind_value);
  int rel = index.data(TraceSliceModel::RelativeRole).toInt();
  const int distance = std::max(1, std::abs(rel));

  QColor base = getThemeColor(InstructionColor);
  QColor accent = base;
  if (kind == TraceSliceEntry::Kind::Past) {
    accent = getThemeColor(CyanStandardHighlightColor);
  } else if (kind == TraceSliceEntry::Kind::Future) {
    accent = getThemeColor(RedStandardHighlightColor);
  } else {
    accent = getThemeColor(InstructionHighlightColor);
  }

  const uint8_t mix = static_cast<uint8_t>(std::min(200, 40 + distance * 12));
  QColor text_color = mixColor(base, accent, mix);
  painter->setPen(text_color);

  if (index.column() == 1) {
    painter->setFont(m_mono);
  } else {
    painter->setFont(opt.font);
  }

  QRect text_rect = opt.rect.adjusted(6, 0, -6, 0);
  Qt::Alignment align = Qt::AlignVCenter | (index.column() == 0 ? Qt::AlignRight : Qt::AlignLeft);
  painter->drawText(text_rect, align, opt.text);
  painter->restore();
}

QSize TraceSliceDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
  Q_UNUSED(option);
  Q_UNUSED(index);
  return QSize(0, m_row_height);
}

TraceSliceView::TraceSliceView(QWidget* parent) : QTableView(parent) {
  m_model = new TraceSliceModel(this);
  auto* delegate = new TraceSliceDelegate(this);

  setModel(m_model);
  setItemDelegate(delegate);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setShowGrid(false);
  setAlternatingRowColors(false);
  verticalHeader()->setVisible(false);
  horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  horizontalHeader()->setStretchLastSection(true);
  setFocusPolicy(Qt::NoFocus);
}

void TraceSliceView::setEntries(const std::vector<TraceSliceEntry>& entries) { m_model->setEntries(entries); }

void TraceSliceView::clear() { m_model->clear(); }

std::optional<uint64_t> TraceSliceView::address_for_row(int row) const {
  if (!m_model) {
    return std::nullopt;
  }
  const TraceSliceEntry* entry = m_model->entryAt(row);
  if (!entry) {
    return std::nullopt;
  }
  return entry->address;
}

} // namespace binja::rewind::ui
