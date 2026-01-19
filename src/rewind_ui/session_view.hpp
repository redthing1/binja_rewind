#pragma once

#include <QAbstractTableModel>
#include <QSplitter>
#include <QTableView>
#include <QWidget>

#include "rewind_core/worker.hpp"

class QLabel;

class ModulesModel : public QAbstractTableModel {
public:
  explicit ModulesModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

  void setModules(const std::vector<binja_rewind::TraceModule>& modules);
  void clear();

private:
  std::vector<binja_rewind::TraceModule> m_modules;
};

class SessionView : public QWidget {
public:
  explicit SessionView(QWidget* parent = nullptr);

  void setSummary(const binja_rewind::TraceSummary& summary);
  void setModules(const std::vector<binja_rewind::TraceModule>& modules);
  void clear();

private:
  QLabel* make_value_label();
  void set_label(QLabel* label, const QString& value);

  QLabel* m_arch = nullptr;
  QLabel* m_target = nullptr;
  QLabel* m_version = nullptr;
  QLabel* m_threads = nullptr;
  QLabel* m_modules = nullptr;
  QLabel* m_features = nullptr;

  ModulesModel* m_modules_model = nullptr;
  QTableView* m_modules_table = nullptr;
};
