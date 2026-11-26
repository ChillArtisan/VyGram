#pragma once

#include <QtCore/QRect>

class QPainter;

namespace Ui {

inline constexpr auto kGlassSurface = 5;

void PaintGlassSurface(QPainter &p, QRect clip);

} // namespace Ui
