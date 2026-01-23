#include "rewind/ui/theme/icon_loader.hpp"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

#include "theme.h"

namespace binja::rewind::ui {

namespace {

QByteArray load_svg_data(const QString& resource_path, const QColor& color) {
  QFile file(resource_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }

  QByteArray data = file.readAll();
  if (color.isValid()) {
    const QByteArray color_name = color.name(QColor::HexRgb).toUtf8();
    data.replace("currentColor", color_name);
  }
  return data;
}

QPixmap render_svg_pixmap(const QByteArray& data, const QSize& size = {}) {
  QSvgRenderer renderer(data);
  QSize render_size = size.isEmpty() ? renderer.defaultSize() : size;
  if (render_size.isEmpty()) {
    render_size = QSize(24, 24);
  }

  QPixmap pixmap(render_size);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  renderer.render(&painter);
  return pixmap;
}

} // namespace

QPixmap make_pixmap(const QString& resource_path, BNThemeColor color) {
  const QColor actual = getThemeColor(color);
  return render_svg_pixmap(load_svg_data(resource_path, actual));
}

QPixmap make_pixmap(const QString& resource_path, const QColor& color) {
  return render_svg_pixmap(load_svg_data(resource_path, color));
}

QIcon make_icon(const QString& resource_path, BNThemeColor color) {
  QIcon icon;
  icon.addPixmap(make_pixmap(resource_path, color), QIcon::Normal);
  icon.addPixmap(make_pixmap(resource_path, SidebarInactiveIconColor), QIcon::Disabled);
  return icon;
}

QIcon make_icon(const QString& resource_path, const QColor& color) {
  QIcon icon;
  icon.addPixmap(make_pixmap(resource_path, color), QIcon::Normal);
  icon.addPixmap(make_pixmap(resource_path, SidebarInactiveIconColor), QIcon::Disabled);
  return icon;
}

QImage make_icon_image(const QString& resource_path, const QColor& color) {
  return render_svg_pixmap(load_svg_data(resource_path, color)).toImage();
}

} // namespace binja::rewind::ui
