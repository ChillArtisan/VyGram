// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_appearance.h"

#include "lang_auto.h"
#include "boxes/background_box.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "settings/sections/settings_chat.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include "ui/layers/generic_box.h"
#include "ui/boxes/single_choice_box.h"
#include "window/themes/window_theme_preview.h"
#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/features/account_appearance/account_appearance.h"
#include "ayu/ui/boxes/account_switcher.h"
#include "ayu/ui/boxes/font_selector.h"
#include "ayu/ui/components/avatar_corners_preview.h"
#include "ayu/ui/components/icon_picker.h"
#include "ayu/ui/glass_surface.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "base/weak_ptr.h"
#include "crl/crl_on_main.h"

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

bool HasDrawerBots(not_null<Window::SessionController*> controller) {
	// todo: maybe iterate through all accounts
	const auto bots = &controller->session().attachWebView();
	for (const auto &bot : bots->attachBots()) {
		if (!bot.inMainMenu || !bot.media) {
			continue;
		}
		return true;
	}
	return false;
}

void BuildAppIcon(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/appIcon"_q,
		.title = tr::ayu_AppIconHeader(),
	});

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<IconPicker>(ctx.container),
			.margin = st::settingsButtonNoIcon.padding,
		};
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	builder.addDivider();
	builder.addSkip();
	ayu.addSettingToggle({
		.id = u"ayu/hideNotificationBadge"_q,
		.title = tr::ayu_HideNotificationBadge(),
		.getter = &AyuSettings::hideNotificationBadge,
		.setter = &AyuSettings::setHideNotificationBadge,
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_HideNotificationBadgeDescription());
	builder.addSkip();
#else
    builder.addDivider();
    builder.addSkip();
#endif
}

void BuildAvatarCorners(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();
	const auto controller = builder.controller();

	const auto mapRadius = [](int val)
	{
		if (val == 0) {
			return tr::ayu_AvatarCornersSquare(tr::now).toUpper();
		} else if (val == AyuUiSettings::kMaxAvatarCorners) {
			return tr::ayu_AvatarCornersCircle(tr::now).toUpper();
		}
		return QString::number(val);
	};

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto container = ctx.container;
		auto title = object_ptr<Ui::FlatLabel>(
			container,
			tr::ayu_AvatarCorners(),
			st::defaultSubsectionTitle);
		const auto titleRaw = title.data();

		const auto badge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				settings->avatarCornersValue() | rpl::map(mapRadius),
				st::settingsPremiumNewBadge),
			st::ayuBetaBadgePadding);
		badge->show();
		badge->setAttribute(Qt::WA_TransparentForMouseEvents);
		badge->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(badge);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			const auto r = st::ayuBetaBadgePadding.left();
			p.drawRoundedRect(badge->rect(), r, r);
		}, badge->lifetime());

		titleRaw->geometryValue() | rpl::on_next([=](QRect geometry) {
			badge->moveToLeft(
				geometry.x()
					+ titleRaw->textMaxWidth()
					+ st::settingsPremiumNewBadgePosition.x(),
				geometry.y()
					+ (geometry.height() - badge->height()) / 2);
		}, badge->lifetime());

		return {
			.widget = std::move(title),
			.margin = st::defaultSubsectionTitlePadding,
		};
	}, [] {
		return SearchEntry{
			.id = u"ayu/avatarCorners"_q,
			.title = tr::ayu_AvatarCorners(tr::now),
		};
	});

	auto *previewRaw = static_cast<AvatarCornersPreview*>(nullptr);
	builder.add([&](const Builder::WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<AvatarCornersPreview>(
			ctx.container,
			controller);
		previewRaw = preview.data();
		const auto vMargin = st::settingsButtonNoIcon.padding
			- st::defaultDialogRow.padding;
		return {
			.widget = std::move(preview),
			.margin = QMargins(0, vMargin.top(), 0, vMargin.bottom()),
		};
	});

	ayu.addSlider({
		.id = u"ayu/avatarCornersSlider"_q,
		.title = rpl::single(QString()),
		.showTitle = false,
		.steps = AyuUiSettings::kMaxAvatarCorners + 1,
		.current = settings->avatarCorners(),
		.onChanged = [=](int val) {
			AyuSettings::getInstance().setAvatarCorners(val);
			if (previewRaw) {
				previewRaw->update();
			}
		},
		.onFinalChanged = [=](int val) {
			AyuSettings::getInstance().setAvatarCorners(val);
			ShowRestartPrompt(controller);
		},
	});

	ayu.addSettingToggle({
		.id = u"ayu/singleCornerRadius"_q,
		.title = tr::ayu_SingleCornerRadius(),
		.getter = &AyuSettings::singleCornerRadius,
		.setter = &AyuSettings::setSingleCornerRadius,
	});

	builder.addSkip();
	builder.addDividerText(tr::ayu_SingleCornerRadiusDescription());
	builder.addSkip();
}

void BuildAppearance(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle(tr::ayu_CategoryAppearance());

	ayu.addSettingToggle({
		.id = u"ayu/materialSwitches"_q,
		.altIds = { u"ayu/newSwitchStyle"_q },
		.title = tr::ayu_MaterialSwitches(),
		.getter = &AyuSettings::materialSwitches,
		.setter = &AyuSettings::setMaterialSwitches,
	});
	ayu.addSettingToggle({
		.id = u"ayu/disableCustomBackgrounds"_q,
		.altIds = { u"ayu/customThemes"_q },
		.title = tr::ayu_DisableCustomBackgrounds(),
		.getter = &AyuSettings::disableCustomBackgrounds,
		.setter = &AyuSettings::setDisableCustomBackgrounds,
	});
	ayu.addSettingToggle({
		.id = u"ayu/hidePremiumStatuses"_q,
		.title = tr::ayu_HidePremiumStatuses(),
		.getter = &AyuSettings::hidePremiumStatuses,
		.setter = &AyuSettings::setHidePremiumStatuses,
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"ayu/monoFont"_q,
		.title = tr::ayu_MonospaceFont(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(
			settings->monoFont().isEmpty()
				? tr::ayu_FontDefault(tr::now)
				: settings->monoFont()),
		.onClick = [=] {
			AyuUi::FontSelectorBox::Show(
				controller,
				[=](const QString &font) {
					AyuSettings::getInstance().setMonoFont(font);
				});
		},
	});

	ayu.addSectionDivider();
}

enum class Preset {
	Graphite,
	ColdSteel,
	Glass,
};

[[nodiscard]] Preset CurrentPreset() {
	return Window::Theme::IsGlassTheme()
		? Preset::Glass
		: Window::Theme::IsColdSteelTheme()
		? Preset::ColdSteel
		: Preset::Graphite;
}

[[nodiscard]] QString PresetPath(Preset preset) {
	switch (preset) {
	case Preset::ColdSteel: return Window::Theme::ColdSteelThemePath();
	case Preset::Glass: return Window::Theme::GlassThemePath();
	case Preset::Graphite: break;
	}
	return Window::Theme::TerminalThemePath();
}

[[nodiscard]] QString PresetName(Preset preset) {
	switch (preset) {
	case Preset::ColdSteel: return u"cold-steel"_q;
	case Preset::Glass: return u"glass"_q;
	case Preset::Graphite: break;
	}
	return u"graphite"_q;
}

[[nodiscard]] std::optional<Preset> PresetFromName(const QString &name) {
	for (const auto preset : { Preset::Graphite, Preset::ColdSteel, Preset::Glass }) {
		if (PresetName(preset) == name) {
			return preset;
		}
	}
	return std::nullopt;
}

void ExportGraphiteAppearance(not_null<Window::SessionController*> controller) {
	auto options = Ayu::AccountAppearance::CaptureOptions();
	options.insert(u"version"_q, 1);
	options.insert(u"preset"_q, Window::Theme::IsTerminalTheme()
		? PresetName(CurrentPreset())
		: u"custom"_q);
	options.insert(u"graphiteThemeActive"_q, Window::Theme::IsTerminalTheme());
	FileDialog::GetWritePath(
		Core::App().getFileDialogParent(),
		tr::ayu_GraphiteExportAppearance(tr::now),
		u"JSON (*.json)"_q,
		u"VyGram-appearance.json"_q,
		crl::guard(controller, [=](QString &&path) {
			if (path.isEmpty()) {
				return;
			}
			auto file = QSaveFile(path);
			const auto bytes = QJsonDocument(options).toJson();
			if (!file.open(QIODevice::WriteOnly)
				|| file.write(bytes) != bytes.size()
				|| !file.commit()) {
				controller->showToast(tr::ayu_GraphiteExportFailed(tr::now));
			} else {
				controller->showToast(tr::ayu_GraphiteExported(tr::now));
			}
		}));
}

bool ApplyEmbeddedPreset(Preset preset) {
	const auto path = PresetPath(preset);
	if (preset == Preset::Glass) {
		const auto background = Window::Theme::Background();
		auto look = background->captureLook();
		if (!look) {
			return false;
		}
		look->theme = Window::Theme::Object{
			.pathRelative = path,
			.pathAbsolute = path,
		};
		look->nightMode = true;
		return background->applyLook(*look) && Window::Theme::IsGlassTheme();
	}
	if (Window::Theme::IsNightMode()) {
		if (!Window::Theme::Apply(path)) {
			return false;
		}
	} else {
		Window::Theme::ToggleNightMode(path);
	}
	if (!Window::Theme::IsTerminalTheme() || CurrentPreset() != preset) {
		return false;
	}
	Window::Theme::KeepApplied();
	return true;
}

bool ImportAppearance(const QByteArray &bytes) {
	const auto data = QJsonDocument::fromJson(bytes).object();
	if (data.value(u"version"_q).toInt(-1) != 1
		|| !Ayu::AccountAppearance::ValidateOptions(data)) {
		return false;
	}
	const auto name = data.value(u"preset"_q);
	if (!name.isUndefined() && (!name.isString()
		|| (!PresetFromName(name.toString()) && name != u"custom"_q))) {
		return false;
	}
	auto preset = PresetFromName(name.toString());
	if (name.isUndefined() && data.value(u"graphiteThemeActive"_q).toBool()) {
		preset = Preset::Graphite;
	}
	if (preset
		&& (!Window::Theme::IsTerminalTheme() || CurrentPreset() != *preset)
		&& !ApplyEmbeddedPreset(*preset)) {
		return false;
	}
	Ayu::AccountAppearance::ApplyOptions(data);
	return true;
}

void ImportGraphiteAppearance(not_null<Window::SessionController*> controller) {
	FileDialog::GetOpenPath(
		Core::App().getFileDialogParent(),
		tr::ayu_GraphiteImportAppearance(tr::now),
		u"JSON (*.json)"_q,
		crl::guard(controller, [=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
				return;
			}
			constexpr auto limit = 64 * 1024;
			auto bytes = std::move(result.remoteContent);
			if (bytes.isEmpty() && !result.paths.isEmpty()) {
				auto file = QFile(result.paths.front());
				if (file.open(QIODevice::ReadOnly)) {
					bytes = file.read(limit + 1);
				}
			}
			const auto success = (bytes.size() <= limit) && ImportAppearance(bytes);
			controller->showToast(success
				? tr::ayu_GraphiteImported(tr::now)
				: tr::ayu_GraphiteImportFailed(tr::now));
		}));
}

void GraphitePreview(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::ayu_GraphitePreview());
	box->setWidth(st::boxWideWidth);
	const auto preview = Window::Theme::GeneratePreview(
		{},
		PresetPath(CurrentPreset()));
	if (!preview.isNull()) {
		const auto picture = box->addRow(object_ptr<Ui::RpWidget>(box));
		picture->widthValue() | rpl::on_next([=](int width) {
			picture->resize(width, width * preview.height() / preview.width());
		}, picture->lifetime());
		picture->paintRequest() | rpl::on_next([=] {
			auto painter = QPainter(picture);
			painter.setRenderHint(QPainter::SmoothPixmapTransform);
			painter.drawImage(picture->rect(), preview);
		}, picture->lifetime());
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void AddSurfaceChoice(
		SectionBuilder &builder,
		QString id,
		rpl::producer<QString> title,
		int (AyuSettings::*get)() const,
		rpl::producer<int> (AyuSettings::*values)() const,
		void (AyuSettings::*set)(int)) {
	const auto settings = &AyuSettings::getInstance();
	const auto options = std::vector<QString>{
		tr::ayu_GraphitePlain(tr::now),
		tr::ayu_GraphiteGrain(tr::now),
		tr::ayu_GraphiteTopo(tr::now),
		tr::ayu_GraphiteNocturnal(tr::now),
		tr::ayu_GraphiteGrid(tr::now),
		tr::ayu_GraphiteGlass(tr::now),
	};
	const auto controller = builder.controller();
	const auto caption = rpl::duplicate(title) | rpl::take(1);
	builder.addButton({
		.id = std::move(id),
		.title = std::move(title),
		.st = &st::settingsButtonNoIcon,
		.label = (settings->*values)() | rpl::map([=](int value) { return options[value]; }),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = rpl::duplicate(caption),
					.options = options,
					.initialSelection = (settings->*get)(),
					.callback = [=](int index) { (settings->*set)(index); },
				});
			}));
		},
	});
}

void BuildTerminalDesign(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	const auto controller = builder.controller();
	builder.addSubsectionTitle(tr::lng_settings_theme_terminal());
	const auto choosePreset = [=](Preset preset) {
		const auto apply = crl::guard(controller, [=] {
			if (!ApplyEmbeddedPreset(preset)) {
				return;
			}
			const auto glass = (preset == Preset::Glass);
			const auto radius = glass ? 12 : 2;
			auto &settings = AyuSettings::getInstance();
			settings.setMessageBubbleRadius(radius);
			settings.setRemoveMessageTail(true);
			if (!glass) {
				settings.setAvatarCorners(6);
			}
			settings.setSingleCornerRadius(true);
			settings.setMonoFont(u"JetBrains Mono"_q);
			if (glass) {
				settings.setGraphiteNavSurface(Ui::kGlassSurface);
				settings.setGraphiteListSurface(Ui::kGlassSurface);
				settings.setGraphiteGrainOpacity(2);
			}
			Ui::SetAppliedBubbleRadius(radius);
		});
		if (Window::Theme::IsNightMode()) {
			apply();
		} else {
			Window::Theme::ToggleNightModeWithConfirmation(&controller->window(), apply);
		}
	};
	builder.addButton({
		.id = u"ayu/terminalDesign"_q,
		.title = tr::lng_settings_theme_terminal_apply(),
		.icon = { &st::menuIconPalette },
		.onClick = [=] { choosePreset(Preset::Graphite); },
	});
	builder.addButton({
		.id = u"ayu/coldSteel"_q,
		.title = tr::ayu_GraphiteApplyColdSteel(),
		.icon = { &st::menuIconPalette },
		.onClick = [=] { choosePreset(Preset::ColdSteel); },
	});
	builder.addButton({
		.id = u"ayu/glass"_q,
		.title = tr::ayu_GraphiteApplyGlass(),
		.icon = { &st::menuIconPalette },
		.onClick = [=] { choosePreset(Preset::Glass); },
	});
	ayu.addSettingToggle({
		.id = u"ayu/graphiteNavLabels"_q,
		.title = tr::ayu_GraphiteNavLabels(),
		.getter = &AyuSettings::graphiteNavLabels,
		.setter = &AyuSettings::setGraphiteNavLabels,
	});
	ayu.addSettingToggle({
		.id = u"ayu/graphiteCompactNav"_q,
		.title = tr::ayu_GraphiteCompactNav(),
		.getter = &AyuSettings::graphiteCompactNav,
		.setter = &AyuSettings::setGraphiteCompactNav,
	});
	ayu.addSettingToggle({
		.id = u"ayu/graphiteForumMessages"_q,
		.title = tr::ayu_GraphiteForumMessages(),
		.getter = &AyuSettings::graphiteForumMessages,
		.setter = &AyuSettings::setGraphiteForumMessages,
	});
	ayu.addSettingToggle({
		.id = u"ayu/graphiteMessageIds"_q,
		.title = tr::ayu_GraphiteMessageIds(),
		.getter = &AyuSettings::graphiteMessageIds,
		.setter = &AyuSettings::setGraphiteMessageIds,
	});
	AddSurfaceChoice(builder, u"ayu/graphiteChatSurface"_q, tr::ayu_GraphiteChatSurface(),
		&AyuSettings::graphiteChatSurface, &AyuSettings::graphiteChatSurfaceValue, &AyuSettings::setGraphiteChatSurface);
	AddSurfaceChoice(builder, u"ayu/graphiteNavSurface"_q, tr::ayu_GraphiteNavSurface(),
		&AyuSettings::graphiteNavSurface, &AyuSettings::graphiteNavSurfaceValue, &AyuSettings::setGraphiteNavSurface);
	AddSurfaceChoice(builder, u"ayu/graphiteListSurface"_q, tr::ayu_GraphiteListSurface(),
		&AyuSettings::graphiteListSurface, &AyuSettings::graphiteListSurfaceValue, &AyuSettings::setGraphiteListSurface);
	ayu.addSlider({
		.id = u"ayu/graphiteMessageSize"_q,
		.title = tr::ayu_GraphiteMessageSize(),
		.steps = 9,
		.current = AyuSettings::getInstance().graphiteMessageSize(),
		.indexToValue = [](int i) { return i + 12; },
		.onChanged = [](int value) { AyuSettings::getInstance().setGraphiteMessageSize(value); },
		.formatLabel = [](int value) { return QString::number(value) + u" px"_q; },
	});
	ayu.addSlider({
		.id = u"ayu/graphiteAvatarSize"_q,
		.title = tr::ayu_GraphiteAvatarSize(),
		.steps = 25,
		.current = AyuSettings::getInstance().graphiteAvatarSize(),
		.indexToValue = [](int i) { return i + 32; },
		.onChanged = [](int value) { AyuSettings::getInstance().setGraphiteAvatarSize(value); },
		.formatLabel = [](int value) { return QString::number(value) + u" px"_q; },
	});
	ayu.addSlider({
		.id = u"ayu/graphiteGrainOpacity"_q,
		.title = tr::ayu_GraphiteGrainOpacity(),
		.steps = 11,
		.current = AyuSettings::getInstance().graphiteGrainOpacity(),
		.indexToValue = [](int i) { return i + 0; },
		.onChanged = [](int value) { AyuSettings::getInstance().setGraphiteGrainOpacity(value); },
		.formatLabel = [](int value) { return QString::number(value) + u"%"_q; },
	});
	builder.addButton({
		.id = u"ayu/accountAppearance"_q,
		.title = tr::lng_vygram_account_appearance(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] { Ayu::ShowAccountAppearanceBox(controller); },
	});
	builder.addButton({
		.id = u"ayu/graphiteWallpaper"_q,
		.title = tr::lng_settings_section_background(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] { controller->show(Box<BackgroundBox>(controller)); },
	});
	builder.addButton({
		.id = u"ayu/graphiteAppearance"_q,
		.title = tr::lng_settings_theme_settings(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] { controller->showSettings(ChatId()); },
	});
	builder.addButton({
		.id = u"ayu/graphitePreview"_q,
		.title = tr::ayu_GraphitePreview(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] { controller->show(Box(GraphitePreview)); },
	});
	builder.addButton({
		.id = u"ayu/graphiteImport"_q,
		.title = tr::ayu_GraphiteImportAppearance(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] { ImportGraphiteAppearance(controller); },
	});
	builder.addButton({
		.id = u"ayu/graphiteExportAppearance"_q,
		.title = tr::ayu_GraphiteExportAppearance(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] { ExportGraphiteAppearance(controller); },
	});
	builder.addDividerText(tr::lng_settings_theme_terminal_description());
	builder.addSkip();
	builder.addDivider();
}

void BuildChatFolders(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::ayu_ChatFoldersHeader());

	ayu.addSettingToggle({
		.id = u"ayu/hideNotificationCounters"_q,
		.altIds = { u"ayu/tabCounter"_q },
		.title = tr::ayu_HideNotificationCounters(),
		.getter = &AyuSettings::hideNotificationCounters,
		.setter = &AyuSettings::setHideNotificationCounters,
	});
	ayu.addSettingToggle({
		.id = u"ayu/hideAllChatsFolder"_q,
		.altIds = { u"ayu/hideAllChats"_q },
		.title = tr::ayu_HideAllChats(),
		.getter = &AyuSettings::hideAllChatsFolder,
		.setter = &AyuSettings::setHideAllChatsFolder,
	});

	ayu.addSectionDivider();
}

void BuildTrayElements(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::ayu_TrayElementsHeader());

	ayu.addSettingToggle({
		.id = u"ayu/showGhostToggleInTray"_q,
		.title = tr::ayu_EnableGhostModeTray(),
		.getter = &AyuSettings::showGhostToggleInTray,
		.setter = &AyuSettings::setShowGhostToggleInTray,
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	ayu.addSettingToggle({
		.id = u"ayu/showStreamerToggleInTray"_q,
		.title = tr::ayu_EnableStreamerModeTray(),
		.getter = &AyuSettings::showStreamerToggleInTray,
		.setter = &AyuSettings::setShowStreamerToggleInTray,
	});
#endif

	ayu.addSectionDivider();
}

void BuildDrawerElements(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::ayu_DrawerElementsHeader());

	ayu.addSettingToggle({
		.id = u"ayu/showMyProfileInDrawer"_q,
		.title = tr::lng_menu_my_profile(),
		.getter = &AyuSettings::showMyProfileInDrawer,
		.setter = &AyuSettings::setShowMyProfileInDrawer,
		.icon = { &st::menuIconProfile },
	});

	const auto controller = builder.controller();
	if (controller && HasDrawerBots(controller)) {
		ayu.addSettingToggle({
			.id = u"ayu/showBotsInDrawer"_q,
			.title = tr::lng_filters_type_bots(),
			.getter = &AyuSettings::showBotsInDrawer,
			.setter = &AyuSettings::setShowBotsInDrawer,
			.icon = { &st::menuIconBot },
		});
	}

	ayu.addSettingToggle({
		.id = u"ayu/showNewGroupInDrawer"_q,
		.title = tr::lng_create_group_title(),
		.getter = &AyuSettings::showNewGroupInDrawer,
		.setter = &AyuSettings::setShowNewGroupInDrawer,
		.icon = { &st::menuIconGroups },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showNewChannelInDrawer"_q,
		.title = tr::lng_create_channel_title(),
		.getter = &AyuSettings::showNewChannelInDrawer,
		.setter = &AyuSettings::setShowNewChannelInDrawer,
		.icon = { &st::menuIconChannel },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showContactsInDrawer"_q,
		.title = tr::lng_menu_contacts(),
		.getter = &AyuSettings::showContactsInDrawer,
		.setter = &AyuSettings::setShowContactsInDrawer,
		.icon = { &st::menuIconUserShow },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showCallsInDrawer"_q,
		.title = tr::lng_menu_calls(),
		.getter = &AyuSettings::showCallsInDrawer,
		.setter = &AyuSettings::setShowCallsInDrawer,
		.icon = { &st::menuIconPhone },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showSavedMessagesInDrawer"_q,
		.title = tr::lng_saved_messages(),
		.getter = &AyuSettings::showSavedMessagesInDrawer,
		.setter = &AyuSettings::setShowSavedMessagesInDrawer,
		.icon = { &st::menuIconSavedMessages },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showLReadToggleInDrawer"_q,
		.title = tr::ayu_LReadMessages(),
		.getter = &AyuSettings::showLReadToggleInDrawer,
		.setter = &AyuSettings::setShowLReadToggleInDrawer,
		.icon = { &st::ayuLReadMenuIcon },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showSReadToggleInDrawer"_q,
		.title = tr::ayu_SReadMessages(),
		.getter = &AyuSettings::showSReadToggleInDrawer,
		.setter = &AyuSettings::setShowSReadToggleInDrawer,
		.icon = { &st::ayuSReadMenuIcon },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showNightModeToggleInDrawer"_q,
		.title = tr::lng_menu_night_mode(),
		.getter = &AyuSettings::showNightModeToggleInDrawer,
		.setter = &AyuSettings::setShowNightModeToggleInDrawer,
		.icon = { &st::menuIconNightMode },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showGhostToggleInDrawer"_q,
		.title = tr::ayu_GhostModeToggle(),
		.getter = &AyuSettings::showGhostToggleInDrawer,
		.setter = &AyuSettings::setShowGhostToggleInDrawer,
		.icon = { &st::ayuGhostIcon },
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	ayu.addSettingToggle({
		.id = u"ayu/showStreamerToggleInDrawer"_q,
		.title = tr::ayu_StreamerModeToggle(),
		.getter = &AyuSettings::showStreamerToggleInDrawer,
		.setter = &AyuSettings::setShowStreamerToggleInDrawer,
		.icon = { &st::ayuStreamerModeMenuIcon },
	});
#endif

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuAppearance::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryAppearance,
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildTerminalDesign(builder, ayu);
	BuildAppIcon(builder, ayu);
	BuildAvatarCorners(builder, ayu);
	BuildAppearance(builder, ayu);
	BuildChatFolders(builder, ayu);
	BuildTrayElements(builder, ayu);
	BuildDrawerElements(builder, ayu);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> AyuAppearance::title() {
	return tr::ayu_CategoryAppearance();
}

AyuAppearance::AyuAppearance(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuAppearanceId() {
	return AyuAppearance::Id();
}

} // namespace Settings
