// Native Graphite surface decoration. All bitmaps are shared across repaints.
#pragma once

#include "ui/style/style_core_scale.h"

#include <QtGui/QPainter>
#include <QtGui/QPixmap>

namespace Ui {

inline void PaintGraphiteSurface(
		QPainter &p,
		QRect bounds,
		QRect clip,
		int surface,
		int grainOpacity) {
	clip = clip.intersected(bounds);
	if (!surface || clip.isEmpty()) {
		return;
	}
	p.save();
	p.setClipRect(clip, Qt::IntersectClip);
	if (surface == 2) {
		static const auto contours = QPixmap(u":/gui/graphite/topographic.png"_q);
		p.setOpacity(0.06);
		p.drawTiledPixmap(clip, contours, clip.topLeft() - bounds.topLeft());
	} else if (surface == 3) {
		static const auto landscape = QPixmap(u":/gui/graphite/navigation.jpg"_q);
		const auto w = std::min(bounds.width(), style::ConvertScale(250));
		const auto h = w * 440 / 170;
		p.setOpacity(0.76);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawPixmap(QRect(bounds.left(), bounds.bottom() - h + 1, w, h), landscape);
	} else if (surface == 4) {
		const auto step = style::ConvertScale(28);
		p.setOpacity(0.07);
		p.setPen(QColor(220, 207, 183));
		const auto origin = clip.topLeft() - bounds.topLeft();
		for (auto x = clip.left() - origin.x() % step; x <= clip.right(); x += step) {
			p.drawLine(x, clip.top(), x, clip.bottom());
		}
		for (auto y = clip.top() - origin.y() % step; y <= clip.bottom(); y += step) {
			p.drawLine(clip.left(), y, clip.right(), y);
		}
	}
	if (grainOpacity > 0) {
		static const auto grain = QPixmap(u":/gui/graphite/grain.png"_q);
		p.setOpacity(std::clamp(grainOpacity, 0, 10) / 100.);
		p.drawTiledPixmap(clip, grain, clip.topLeft() - bounds.topLeft());
	}
	p.restore();
}

} // namespace Ui
