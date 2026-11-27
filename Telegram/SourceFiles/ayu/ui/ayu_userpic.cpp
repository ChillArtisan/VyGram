// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/ayu_userpic.h"

#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "media/streaming/media_streaming_common.h"
#include "ui/image/image_prepare.h"
#include "ui/style/style_core.h"
#include "base/algorithm.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "ui/painter.h"
#include "window/themes/window_theme.h"

#include <QPainterPath>

#include <QRectF>

namespace AyuUserpic {

bool ShouldOverrideShape(Ui::PeerUserpicShape shape) {
	using Shape = Ui::PeerUserpicShape;
	switch (shape) {
	case Shape::Circle:
	case Shape::Auto:
		return true;
	case Shape::Monoforum:
	case Shape::Forum:
		return AyuSettings::getInstance().singleCornerRadius();
	}
	return false;
}

int ComputeRadius(int pixelSize) {
	const auto corners = AyuUiSettings::getAvatarCorners();
	if (corners >= AyuUiSettings::kMaxAvatarCorners) return pixelSize / 2;
	if (corners <= 0) return 0;
	return int(double(corners) / AyuUiSettings::kMaxAvatarCorners * pixelSize / 2.0);
}

double ComputeRadiusF(double size) {
	const auto corners = AyuUiSettings::getAvatarCorners();
	if (corners >= AyuUiSettings::kMaxAvatarCorners) return size / 2.0;
	if (corners <= 0) return 0.0;
	return double(corners) / AyuUiSettings::kMaxAvatarCorners * size / 2.0;
}

bool IsCircle() {
	return AyuUiSettings::getAvatarCorners() >= AyuUiSettings::kMaxAvatarCorners;
}

namespace {

enum TerminalShape {
	kTerminalNone,
	kTerminalPerson,
	kTerminalBot,
	kTerminalGroup,
	kTerminalSupergroup,
	kTerminalChannel,
};

[[nodiscard]] QPainterPath TerminalShapePath(int kind, QRectF r) {
	auto path = QPainterPath();
	const auto w = r.width();
	const auto h = r.height();
	const auto polygon = [&](std::initializer_list<QPointF> points) {
		auto poly = std::vector<QPointF>();
		for (const auto &point : points) {
			poly.push_back(r.topLeft() + QPointF(point.x() * w, point.y() * h));
		}
		const auto radius = w * 0.105;
		const auto toward = [&](QPointF from, QPointF to) {
			const auto delta = to - from;
			const auto length = std::hypot(delta.x(), delta.y());
			return from + delta * (radius / length);
		};
		const auto count = int(poly.size());
		for (auto i = 0; i != count; ++i) {
			const auto &corner = poly[i];
			const auto &prev = poly[(i + count - 1) % count];
			const auto &next = poly[(i + 1) % count];
			const auto entry = toward(corner, prev);
			if (!i) {
				path.moveTo(entry);
			} else {
				path.lineTo(entry);
			}
			path.quadTo(corner, toward(corner, next));
		}
		path.closeSubpath();
	};
	switch (kind) {
	case kTerminalPerson:
		path.addEllipse(r);
		break;
	case kTerminalBot:
		path.addRoundedRect(r, w * 0.3, h * 0.3);
		break;
	case kTerminalGroup:
		path.addRoundedRect(r, w * 0.14, h * 0.14);
		break;
	case kTerminalSupergroup:
		polygon({ { 0., 0. }, { 0.68, 0. }, { 1., 0.32 }, { 1., 1. }, { 0., 1. } });
		break;
	case kTerminalChannel: {
		const auto big = w * 0.42;
		const auto tail = w * 0.06;
		path.moveTo(r.left() + big, r.top());
		path.lineTo(r.right() - big, r.top());
		path.arcTo(QRectF(r.right() - 2 * big, r.top(), 2 * big, 2 * big), 90, -90);
		path.lineTo(r.right(), r.bottom() - big);
		path.arcTo(QRectF(r.right() - 2 * big, r.bottom() - 2 * big, 2 * big, 2 * big), 0, -90);
		path.lineTo(r.left() + tail, r.bottom());
		path.arcTo(QRectF(r.left(), r.bottom() - 2 * tail, 2 * tail, 2 * tail), 270, -90);
		path.lineTo(r.left(), r.top() + big);
		path.arcTo(QRectF(r.left(), r.top(), 2 * big, 2 * big), 180, -90);
		path.closeSubpath();
	} break;
	default:
		path.addRect(r);
		break;
	}
	return path;
}

} // namespace

int TerminalShapeKind(not_null<const PeerData*> peer) {
	if (!Window::Theme::IsTerminalTheme()) {
		return kTerminalNone;
	} else if (const auto user = peer->asUser()) {
		return user->isBot() ? kTerminalBot : kTerminalPerson;
	} else if (peer->isChat()) {
		return kTerminalGroup;
	} else if (const auto channel = peer->asChannel()) {
		return channel->isBroadcast() ? kTerminalChannel : kTerminalSupergroup;
	}
	return kTerminalNone;
}

QImage ApplyTerminalShape(QImage image, int kind) {
	auto result = QImage(image.size(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(image.devicePixelRatio());
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	p.drawPath(TerminalShapePath(
		kind,
		QRectF(QPointF(), QSizeF(image.size()) / image.devicePixelRatio())));
	p.setCompositionMode(QPainter::CompositionMode_SourceIn);
	p.drawImage(0, 0, image);
	p.end();
	return result;
}

uint8 PackedState() {
	return uint8(AyuUiSettings::getAvatarCorners() & 0x1F)
		| (AyuSettings::getInstance().singleCornerRadius() ? 0x20 : 0);
}

void PaintShape(QPainter &p, int x, int y, int size) {
	const auto corners = AyuUiSettings::getAvatarCorners();
	if (corners >= AyuUiSettings::kMaxAvatarCorners) {
		p.drawEllipse(x, y, size, size);
	} else if (corners <= 0) {
		p.drawRect(x, y, size, size);
	} else {
		const auto r = double(corners) / AyuUiSettings::kMaxAvatarCorners * size / 2.0;
		p.drawRoundedRect(x, y, size, size, r, r);
	}
}

void PaintShape(QPainter &p, const QRectF &rect) {
	const auto corners = AyuUiSettings::getAvatarCorners();
	if (corners >= AyuUiSettings::kMaxAvatarCorners) {
		p.drawEllipse(rect);
	} else if (corners <= 0) {
		p.drawRect(rect);
	} else {
		const auto r = double(corners) / AyuUiSettings::kMaxAvatarCorners
			* std::min(rect.width(), rect.height()) / 2.0;
		p.drawRoundedRect(rect, r, r);
	}
}

void PaintShape(
		QPainter &p,
		const QRectF &rect,
		not_null<const PeerData*> peer) {
	if (const auto kind = TerminalShapeKind(peer)) {
		p.drawPath(TerminalShapePath(kind, rect));
	} else {
		PaintShape(p, rect);
	}
}

QPointF OnlineBadgePosition(int photoSize, double badgeSize, double stroke) {
	const auto corners = AyuUiSettings::getAvatarCorners();
	const auto r = double(corners) / AyuUiSettings::kMaxAvatarCorners * photoSize / 2.0;
	const auto edge = photoSize - r * (1.0 - std::cos(M_PI / 4.0));
	const auto maxPos = (stroke > 0)
		? photoSize - badgeSize / 2.0 - (badgeSize / 2.0 + stroke / 2.0) / std::sqrt(2.0)
		: double(photoSize) - badgeSize;
	const auto pos = std::min(edge - badgeSize / 2.0, maxPos);
	return QPointF(pos, pos);
}

QRect OnlineBadgeRect(int photoSize, int badgeSize, int stroke) {
	const auto pos = OnlineBadgePosition(photoSize, badgeSize, stroke);
	return QRect(
		int(base::SafeRound(pos.x())),
		int(base::SafeRound(pos.y())),
		badgeSize,
		badgeSize);
}

void ApplyFrameRounding(
		::Media::Streaming::FrameRequest &request,
		std::array<QImage, 4> &cornersCache,
		QImage &ellipseCache,
		QSize size) {
	const auto minSide = std::min(size.width(), size.height());
	const auto r = ComputeRadius(minSide);
	const auto ratio = style::DevicePixelRatio();
	if (r > 0 && r < minSide / 2) {
		if (cornersCache[0].width() != r * ratio) {
			cornersCache = Images::CornersMask(r);
		}
		request.rounding = Images::CornersMaskRef(cornersCache);
	} else if (r >= minSide / 2) {
		if (ellipseCache.size() != request.outer) {
			ellipseCache = Images::EllipseMask(size);
		}
		request.mask = ellipseCache;
	}
}

} // namespace AyuUserpic
