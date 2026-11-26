// Native Graphite surface decoration. All bitmaps are shared across repaints.
#pragma once

#include "ayu/ui/glass_surface.h"
#include "ui/style/style_core_scale.h"
#include "styles/palette.h"

#include <QtGui/QPainter>
#include <QtGui/QPixmap>

#include <vector>

namespace Ui {
namespace details {

inline void ComposeGraphiteSurface(
		QPainter &p,
		QRect bounds,
		int surface,
		int grainOpacity,
		QColor line) {
	if (surface == 2) {
		static const auto contours = QPixmap(u":/gui/graphite/topographic.png"_q);
		p.setOpacity(0.06);
		p.drawTiledPixmap(bounds, contours);
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
		p.setPen(line);
		for (auto x = bounds.left(); x <= bounds.right(); x += step) {
			p.drawLine(x, bounds.top(), x, bounds.bottom());
		}
		for (auto y = bounds.top(); y <= bounds.bottom(); y += step) {
			p.drawLine(bounds.left(), y, bounds.right(), y);
		}
	}
	if (grainOpacity > 0) {
		static const auto grain = QPixmap(u":/gui/graphite/grain.png"_q);
		p.setOpacity(std::clamp(grainOpacity, 0, 10) / 100.);
		p.drawTiledPixmap(bounds, grain);
	}
}

struct GraphiteSurfaceCache {
	QSize size;
	qreal ratio = 0.;
	int surface = 0;
	int grain = 0;
	QRgb line = 0;
	QPixmap pixmap;
};

// WHY: blending the tiled textures with opacity on every scroll frame
// costs a full-window composite, so each surface is composed once per
// size and later frames only blit the prepared premultiplied pixmap.
inline const QPixmap &GraphiteSurfacePixmap(
		QSize size,
		qreal ratio,
		int surface,
		int grainOpacity,
		QColor line) {
	static auto cache = std::vector<GraphiteSurfaceCache>();
	for (auto i = begin(cache); i != end(cache); ++i) {
		if (i->size == size
			&& i->ratio == ratio
			&& i->surface == surface
			&& i->grain == grainOpacity
			&& i->line == line.rgba()) {
			if (i != begin(cache)) {
				std::rotate(begin(cache), i, i + 1);
			}
			return cache.front().pixmap;
		}
	}
	constexpr auto kMaxCached = 8;
	if (cache.size() >= kMaxCached) {
		cache.pop_back();
	}
	auto image = QImage(
		size * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto q = QPainter(&image);
		ComposeGraphiteSurface(
			q,
			QRect(QPoint(), size),
			surface,
			grainOpacity,
			line);
	}
	cache.insert(begin(cache), GraphiteSurfaceCache{
		.size = size,
		.ratio = ratio,
		.surface = surface,
		.grain = grainOpacity,
		.line = line.rgba(),
		.pixmap = QPixmap::fromImage(std::move(image)),
	});
	return cache.front().pixmap;
}

} // namespace details

inline void PaintGraphiteSurface(
		QPainter &p,
		QRect bounds,
		QRect clip,
		int surface,
		int grainOpacity) {
	clip = clip.intersected(bounds);
	if (!surface || clip.isEmpty() || bounds.isEmpty()) {
		return;
	} else if (surface == kGlassSurface) {
		PaintGlassSurface(p, clip);
		if (!grainOpacity) {
			return;
		}
		surface = 0;
	}
	const auto ratio = p.device() ? p.device()->devicePixelRatioF() : 1.;
	const auto &pixmap = details::GraphiteSurfacePixmap(
		bounds.size(),
		ratio,
		surface,
		grainOpacity,
		st::windowFg->c);
	const auto from = clip.translated(-bounds.topLeft());
	p.drawPixmap(
		QRectF(clip),
		pixmap,
		QRectF(
			from.x() * ratio,
			from.y() * ratio,
			from.width() * ratio,
			from.height() * ratio));
}

} // namespace Ui
