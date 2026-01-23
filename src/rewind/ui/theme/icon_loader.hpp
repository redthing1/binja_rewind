#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>

#include "theme.h"

namespace binja::rewind::ui {

QIcon make_icon(const QString& resource_path, BNThemeColor color = SidebarActiveIconColor);
QIcon make_icon(const QString& resource_path, const QColor& color);

QPixmap make_pixmap(const QString& resource_path, BNThemeColor color = SidebarActiveIconColor);
QPixmap make_pixmap(const QString& resource_path, const QColor& color);
QImage make_icon_image(const QString& resource_path, const QColor& color = QColor(Qt::white));

} // namespace binja::rewind::ui
