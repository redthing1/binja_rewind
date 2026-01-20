#pragma once

#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QTableView>

#include "rewind/core/model/replay_types.hpp"

namespace binja::rewind::ui {

class RegistersModel : public QAbstractTableModel {
public:
  explicit RegistersModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

  void setRegisters(const std::vector<binja::rewind::core::model::RegisterValue>& regs);
  void clear();

  enum Role { KnownRole = Qt::UserRole + 1 };

private:
  std::vector<binja::rewind::core::model::RegisterValue> m_registers;
};

class RegistersDelegate : public QStyledItemDelegate {
public:
  explicit RegistersDelegate(QObject* parent = nullptr);

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
  QFont m_mono;
  int m_row_height = 0;
};

class RegistersView : public QTableView {
public:
  explicit RegistersView(QWidget* parent = nullptr);

  void setRegisters(const std::vector<binja::rewind::core::model::RegisterValue>& regs);
  void clear();

private:
  RegistersModel* m_model = nullptr;
};

} // namespace binja::rewind::ui
