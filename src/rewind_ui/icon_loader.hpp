#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>

#include "theme.h"

namespace rewind_ui {

QIcon make_icon(const QString& resource_path, BNThemeColor color = SidebarActiveIconColor);
QIcon make_icon(const QString& resource_path, const QColor& color);

QPixmap make_pixmap(const QString& resource_path, BNThemeColor color = SidebarActiveIconColor);
QPixmap make_pixmap(const QString& resource_path, const QColor& color);

} // namespace rewind_ui
