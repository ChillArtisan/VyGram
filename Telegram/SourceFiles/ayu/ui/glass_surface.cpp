#include "ayu/ui/glass_surface.h"

#include "ui/image/image_prepare.h"
#include "window/themes/window_theme.h"
#include "styles/palette.h"

#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtWidgets/QWidget>

namespace Ui {
namespace {

constexpr auto kDownscale = 8;
constexpr auto kBlurRadius = 6;
constexpr auto kTintOpacity = 0.6;
constexpr auto kHighlightAlpha = 14;

struct Backdrop {
	QSize size;
	qreal ratio = 0.;
	qint64 source = 0;
	QRgb tint = 0;
	QImage image;
};

[[nodiscard]] QImage CoverScaled(const QImage &image, QSize size) {
	if (image.isNull()) {
		auto result = QImage(size, QImage::Format_ARGB32_Premultiplied);
		result.fill(st::windowBg->c);
		return result;
	}
	const auto scaled = image.scaled(
		size,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	return scaled.copy(
		(scaled.width() - size.width()) / 2,
		(scaled.height() - size.height()) / 2,
		size.width(),
		size.height());
}

[[nodiscard]] const QImage &GlassBackdrop(QSize size, qreal ratio) {
	static auto cache = Backdrop();
	const auto background = Window::Theme::Background();
	const auto fill = background->colorForFill();
	const auto source = fill
		? qint64(fill->rgba())
		: (background->prepared().cacheKey()
			^ (background->gradientForFill().cacheKey() << 1));
	const auto tint = st::windowBg->c.rgba();
	if (cache.size == size
		&& cache.ratio == ratio
		&& cache.source == source
		&& cache.tint == tint
		&& !cache.image.isNull()) {
		return cache.image;
	}
	const auto pixels = size * ratio;
	const auto small = QSize(
		std::max(1, pixels.width() / kDownscale),
		std::max(1, pixels.height() / kDownscale));
	auto image = QImage();
	if (fill) {
		image = QImage(small, QImage::Format_ARGB32_Premultiplied);
		image.fill(*fill);
	} else {
		image = CoverScaled(background->createCurrentImage(), small);
	}
	image = Images::BlurLargeImage(
		image.convertToFormat(QImage::Format_ARGB32_Premultiplied),
		kBlurRadius);
	image = image.scaled(
		pixels,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation
	).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	{
		auto q = QPainter(&image);
		auto color = st::windowBg->c;
		color.setAlphaF(kTintOpacity);
		q.fillRect(image.rect(), color);
		auto highlight = QLinearGradient(0, 0, 0, image.height());
		highlight.setColorAt(0., QColor(255, 255, 255, kHighlightAlpha));
		highlight.setColorAt(1., QColor(255, 255, 255, 0));
		q.fillRect(image.rect(), highlight);
	}
	image.setDevicePixelRatio(ratio);
	cache = Backdrop{
		.size = size,
		.ratio = ratio,
		.source = source,
		.tint = tint,
		.image = std::move(image),
	};
	return cache.image;
}

} // namespace

void PaintGlassSurface(QPainter &p, QRect clip) {
	const auto widget = dynamic_cast<QWidget*>(p.device());
	if (!widget || clip.isEmpty()) {
		return;
	}
	const auto window = widget->window();
	const auto ratio = widget->devicePixelRatioF();
	const auto &backdrop = GlassBackdrop(window->size(), ratio);
	const auto inWidget = p.transform().mapRect(clip);
	const auto origin = widget->mapTo(window, inWidget.topLeft());
	p.drawImage(
		QRectF(clip),
		backdrop,
		QRectF(
			origin.x() * ratio,
			origin.y() * ratio,
			clip.width() * ratio,
			clip.height() * ratio));
}

} // namespace Ui
