#include "rewind/ui/widgets/elided_label.hpp"

#include <QFontMetrics>
#include <QResizeEvent>

namespace binja::rewind::ui {

ElidedLabel::ElidedLabel(QWidget* parent) : QLabel(parent) {
  setTextInteractionFlags(Qt::TextSelectableByMouse);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ElidedLabel::setFullText(const QString& text) {
  m_fullText = text;
  setToolTip(text);
  update_elide();
}

void ElidedLabel::resizeEvent(QResizeEvent* event) {
  QLabel::resizeEvent(event);
  update_elide();
}

void ElidedLabel::update_elide() {
  if (m_fullText.isEmpty()) {
    setText(QString());
    return;
  }

  QFontMetrics metrics(font());
  QString elided = metrics.elidedText(m_fullText, Qt::ElideMiddle, width());
  setText(elided);
}

} // namespace binja::rewind::ui
