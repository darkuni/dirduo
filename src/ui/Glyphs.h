#pragma once

#include <QColor>
#include <QIcon>

// Simple line icons drawn in a given color, for the location bar: they stay
// readable on the bar's role color in every theme, and do not depend on the
// icon theme having them.
namespace Glyphs {

enum class Glyph { Back, Forward, Up, Refresh, Home, Star, Folder, ChevronDown, Menu, Swap, Close };

QIcon icon(Glyph g, const QColor &color);
QPixmap pixmap(Glyph g, const QColor &color, int size, qreal dpr = 1.0);

} // namespace Glyphs
