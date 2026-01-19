#include "rewind_ui/registers_view.hpp"

#include <QHeaderView>
#include <QPainter>

#include "fontsettings.h"
#include "theme.h"

RegistersModel::RegistersModel(QObject* parent) : QAbstractTableModel(parent) {}

int RegistersModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(m_registers.size());
}

int RegistersModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return 2;
}

QVariant RegistersModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }
  if (index.row() < 0 || static_cast<size_t>(index.row()) >= m_registers.size()) {
    return QVariant();
  }

  const auto& reg = m_registers[static_cast<size_t>(index.row())];
  if (role == Qt::DisplayRole) {
    if (index.column() == 0) {
      return QString::fromStdString(reg.name);
    }
    if (index.column() == 1) {
      return QString::fromStdString(reg.value);
    }
  }

  if (role == KnownRole) {
    return reg.known;
  }

  if (role == Qt::TextAlignmentRole && index.column() == 1) {
    return QVariant::fromValue(int(Qt::AlignRight | Qt::AlignVCenter));
  }

  return QVariant();
}

QVariant RegistersModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (role != Qt::DisplayRole) {
    return QVariant();
  }
  if (orientation == Qt::Vertical) {
    return QVariant();
  }
  if (section == 0) {
    return QStringLiteral("Register");
  }
  if (section == 1) {
    return QStringLiteral("Value");
  }
  return QVariant();
}

void RegistersModel::setRegisters(const std::vector<binja_rewind::RegisterValue>& regs) {
  beginResetModel();
  m_registers = regs;
  endResetModel();
}

void RegistersModel::clear() {
  beginResetModel();
  m_registers.clear();
  endResetModel();
}

RegistersDelegate::RegistersDelegate(QObject* parent) : QStyledItemDelegate(parent) {
  m_mono = getMonospaceFont(dynamic_cast<QWidget*>(parent));
  QFontMetrics metrics(m_mono);
  m_row_height = metrics.height() + 6;
}

void RegistersDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  QStyleOptionViewItem opt(option);
  initStyleOption(&opt, index);

  painter->save();
  painter->setClipRect(opt.rect);
  painter->fillRect(opt.rect, opt.backgroundBrush);

  const bool known = index.data(RegistersModel::KnownRole).toBool();
  QColor text_color = getThemeColor(InstructionColor);
  if (index.column() == 0) {
    text_color = getThemeColor(RegisterColor);
  } else {
    text_color = known ? getThemeColor(AddressColor) : getThemeColor(UncertainColor);
  }

  painter->setPen(text_color);
  if (index.column() == 1) {
    painter->setFont(m_mono);
  } else {
    painter->setFont(opt.font);
  }

  QRect text_rect = opt.rect.adjusted(6, 0, -6, 0);
  Qt::Alignment align = Qt::AlignVCenter;
  if (index.column() == 1) {
    align |= Qt::AlignRight;
  } else {
    align |= Qt::AlignLeft;
  }

  painter->drawText(text_rect, align, opt.text);
  painter->restore();
}

QSize RegistersDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
  Q_UNUSED(option);
  Q_UNUSED(index);
  return QSize(0, m_row_height);
}

RegistersView::RegistersView(QWidget* parent) : QTableView(parent) {
  m_model = new RegistersModel(this);
  auto* delegate = new RegistersDelegate(this);

  setModel(m_model);
  setItemDelegate(delegate);
  setSelectionMode(QAbstractItemView::NoSelection);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setShowGrid(false);
  setAlternatingRowColors(false);
  verticalHeader()->setVisible(false);
  horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  horizontalHeader()->setStretchLastSection(true);
  setFocusPolicy(Qt::NoFocus);
}

void RegistersView::setRegisters(const std::vector<binja_rewind::RegisterValue>& regs) { m_model->setRegisters(regs); }

void RegistersView::clear() { m_model->clear(); }
