#pragma once

#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QTableView>

#include <optional>
#include <vector>

#include "binaryninjaapi.h"

namespace binja::rewind::ui {

struct TraceSliceEntry {
  int relative = 0;
  uint64_t address = 0;
  QString text;
  enum class Kind { Past, Current, Future } kind = Kind::Current;
};

class TraceSliceModel : public QAbstractTableModel {
public:
  explicit TraceSliceModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

  void setEntries(const std::vector<TraceSliceEntry>& entries);
  void clear();

  enum Role { KindRole = Qt::UserRole + 1, RelativeRole };

  const TraceSliceEntry* entryAt(int row) const;

private:
  std::vector<TraceSliceEntry> m_entries;
};

class TraceSliceDelegate : public QStyledItemDelegate {
public:
  explicit TraceSliceDelegate(QObject* parent = nullptr);

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
  QFont m_mono;
  int m_row_height = 0;
};

class TraceSliceView : public QTableView {
public:
  explicit TraceSliceView(QWidget* parent = nullptr);

  void setEntries(const std::vector<TraceSliceEntry>& entries);
  void clear();

  std::optional<uint64_t> address_for_row(int row) const;

private:
  TraceSliceModel* m_model = nullptr;
};

} // namespace binja::rewind::ui
