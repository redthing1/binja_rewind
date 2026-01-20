#pragma once

#include <QLabel>

namespace binja::rewind::ui {

class ElidedLabel : public QLabel {
public:
  explicit ElidedLabel(QWidget* parent = nullptr);

  void setFullText(const QString& text);
  QString fullText() const { return m_fullText; }

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  void update_elide();

  QString m_fullText;
};

} // namespace binja::rewind::ui
