#include "ayu/ui/boxes/account_switcher.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/account_appearance/account_appearance.h"
#include "ayu/ui/ayu_userpic.h"
#include "boxes/background_box.h"
#include "boxes/premium_limits_box.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "mtproto/mtp_instance.h"
#include "settings/sections/settings_chat.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtGui/QtEvents>

namespace Ayu {
namespace {

[[nodiscard]] std::vector<not_null<Main::Account*>> SwitchableAccounts() {
	auto result = std::vector<not_null<Main::Account*>>();
	for (const auto account : Core::App().domain().orderedAccounts()) {
		if (account->sessionExists()) {
			result.push_back(account);
		}
	}
	return result;
}

void SwitchNow(
		not_null<Window::SessionController*> controller,
		not_null<Main::Account*> account,
		bool newWindow) {
	if (account == &controller->session().account()) {
		return;
	}
	const auto app = &Core::App();
	if (const auto window = app->separateWindowFor(account)) {
		window->activate();
		return;
	} else if (newWindow) {
		app->ensureSeparateWindowFor(account);
	}
	app->domain().maybeActivate(account);
}

void ShowStorageError(not_null<Window::SessionController*> controller) {
	controller->showToast(tr::lng_vygram_chat_privacy_storage_error(tr::now));
}

void AddAccentPicker(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		uint64 sessionUniqueId) {
	const auto &choices = AccountAppearance::AccentChoices();
	const auto count = int(choices.size()) + 1;
	const auto &padding = st::vygramAccountColorPadding;
	const auto size = st::vygramAccountColorSize;
	const auto skip = st::vygramAccountColorSkip;
	const auto picker = container->add(object_ptr<Ui::RpWidget>(container));
	picker->resize(
		padding.left() + count * size + (count - 1) * skip + padding.right(),
		padding.top() + size + padding.bottom());
	picker->setCursor(style::cur_pointer);
	const auto rectFor = [=](int index) {
		return QRect(
			padding.left() + index * (size + skip),
			padding.top(),
			size,
			size);
	};
	const auto selectedIndex = [=] {
		const auto accent = AccountAppearance::Accent(sessionUniqueId);
		if (!accent) {
			return 0;
		}
		const auto i = ranges::find(choices, *accent);
		return (i == end(choices)) ? -1 : int(i - begin(choices)) + 1;
	};
	picker->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(picker);
		auto hq = PainterHighQualityEnabler(p);
		const auto selected = selectedIndex();
		const auto ring = st::vygramAccountRingWidth;
		for (auto i = 0; i != count; ++i) {
			const auto rect = rectFor(i);
			const auto inner = (i == selected)
				? rect.marginsRemoved({ 2 * ring, 2 * ring, 2 * ring, 2 * ring })
				: rect;
			if (i == 0) {
				auto pen = st::windowSubTextFg->p;
				pen.setWidthF(ring);
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
				p.drawEllipse(inner);
				p.drawLine(inner.bottomLeft(), inner.topRight());
			} else {
				p.setPen(Qt::NoPen);
				p.setBrush(choices[i - 1]);
				p.drawEllipse(inner);
			}
			if (i == selected) {
				auto pen = st::windowActiveTextFg->p;
				pen.setWidthF(ring);
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
				p.drawEllipse(QRectF(rect).marginsRemoved(
					{ ring / 2., ring / 2., ring / 2., ring / 2. }));
			}
		}
	}, picker->lifetime());
	picker->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return (e->type() == QEvent::MouseButtonRelease);
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto position = static_cast<QMouseEvent*>(e.get())->pos();
		for (auto i = 0; i != count; ++i) {
			if (!rectFor(i).contains(position)) {
				continue;
			}
			const auto color = (i == 0)
				? std::optional<QColor>()
				: std::make_optional(choices[i - 1]);
			if (!AccountAppearance::SetAccent(sessionUniqueId, color)) {
				ShowStorageError(controller);
			}
			picker->update();
			return;
		}
	}, picker->lifetime());
}

void AccountAppearanceBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	box->setTitle(tr::lng_vygram_account_appearance());
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();

	const auto own = container->add(object_ptr<Ui::SettingsButton>(
		container,
		tr::lng_vygram_account_own_look(),
		st::settingsButtonNoIcon));
	own->toggleOn(rpl::single(AccountAppearance::HasOwn(session)));
	own->toggledChanges(
	) | rpl::filter([=](bool value) {
		return (value != AccountAppearance::HasOwn(session));
	}) | rpl::on_next([=](bool value) {
		if (!AccountAppearance::SetOwn(session, value)) {
			ShowStorageError(controller);
		}
	}, own->lifetime());
	Ui::AddDividerText(container, tr::lng_vygram_account_own_look_about());
	Ui::AddSkip(container);

	const auto wallpapers = container->add(object_ptr<Ui::SettingsButton>(
		container,
		tr::lng_vygram_account_wallpapers(),
		st::settingsButtonNoIcon));
	wallpapers->toggleOn(rpl::single(
		!AccountAppearance::WallpapersHidden(session)));
	wallpapers->toggledChanges(
	) | rpl::filter([=](bool shown) {
		return (shown == AccountAppearance::WallpapersHidden(session));
	}) | rpl::on_next([=](bool shown) {
		if (!AccountAppearance::SetWallpapersHidden(session, !shown)) {
			ShowStorageError(controller);
		}
	}, wallpapers->lifetime());
	container->add(object_ptr<Ui::SettingsButton>(
		container,
		tr::lng_settings_section_background(),
		st::settingsButtonNoIcon
	))->setClickedCallback([=] {
		controller->show(Box<BackgroundBox>(controller));
	});
	container->add(object_ptr<Ui::SettingsButton>(
		container,
		tr::lng_vygram_account_theme(),
		st::settingsButtonNoIcon
	))->setClickedCallback([=] {
		box->closeBox();
		controller->showSettings(Settings::ChatId());
	});
	Ui::AddDividerText(container, tr::lng_vygram_account_wallpapers_about());
	Ui::AddSkip(container);

	Ui::AddSubsectionTitle(container, tr::lng_vygram_account_accent());
	AddAccentPicker(container, controller, session->uniqueId());
	Ui::AddDividerText(container, tr::lng_vygram_account_accent_about());

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void AccountAccentBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<Main::Session*> session) {
	box->setTitle(rpl::single(session->user()->name()));
	AddAccentPicker(box->verticalLayout(), controller, session->uniqueId());
	Ui::AddDividerText(
		box->verticalLayout(),
		tr::lng_vygram_account_accent_about());
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void SwitchToAccount(
		not_null<Window::SessionController*> controller,
		not_null<Main::Account*> account,
		bool newWindow) {
	const auto weakController = base::make_weak(controller.get());
	const auto weakAccount = base::make_weak(account.get());
	crl::on_main([=] {
		const auto strongController = weakController.get();
		const auto strongAccount = weakAccount.get();
		if (strongController && strongAccount) {
			SwitchNow(strongController, strongAccount, newWindow);
		}
	});
}

bool SwitchAccountRelative(
		not_null<Window::SessionController*> controller,
		int delta) {
	const auto list = SwitchableAccounts();
	const auto count = int(list.size());
	if (count < 2) {
		return false;
	}
	const auto i = ranges::find(list, &controller->session().account());
	if (i == end(list)) {
		return false;
	}
	const auto index = (int(i - begin(list)) + delta % count + count) % count;
	SwitchToAccount(controller, list[index], false);
	return true;
}

void AddAccount(not_null<Window::SessionController*> controller) {
	using Environment = MTP::Environment;
	auto &domain = controller->session().domain();
	domain.removeRedundantAccounts();
	auto found = false;
	for (const auto &[index, account] : domain.accounts()) {
		if (!account->sessionExists()
			&& account->mtp().environment() == Environment::Production) {
			found = true;
		}
	}
	if (!found && domain.accounts().size() >= domain.maxAccounts()) {
		controller->show(Box(AccountsLimitBox, &controller->session()));
		return;
	}
	controller->window().preventOrInvoke([=] {
		Core::App().setActivePrimaryWindow(&controller->window());
		controller->session().domain().addActivated(Environment::Production);
	});
}

void ShowAccountsMenu(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		QPoint globalPosition) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		parent.get(),
		st::popupMenuWithIcons);
	const auto add = Ui::Menu::CreateAddActionCallback(menu);
	const auto current = &controller->session().account();
	const auto hideCounters = AyuSettings::getInstance().hideNotificationCounters();
	for (const auto account : SwitchableAccounts()) {
		const auto session = &account->session();
		auto text = session->user()->name();
		const auto unread = session->data().unreadWithMentionsBadge();
		if (account != current && unread > 0 && !hideCounters) {
			text += u" · "_q + QString::number(unread);
		}
		add(text, [=] {
			SwitchToAccount(controller, account, false);
		}, (account == current) ? &st::menuIconSelect : &st::menuIconProfile);
	}
	menu->addSeparator();
	add(tr::lng_menu_add_account(tr::now), [=] {
		AddAccount(controller);
	}, &st::menuIconAddAccount);
	add(tr::lng_vygram_account_appearance(tr::now), [=] {
		ShowAccountAppearanceBox(controller);
	}, &st::menuIconPalette);
	add(tr::lng_main_menu(tr::now), [=] {
		controller->widget()->showMainMenu();
	}, &st::menuIconSettings);
	menu->popup(globalPosition);
}

void ShowAccountAppearanceBox(
		not_null<Window::SessionController*> controller) {
	controller->show(Box(AccountAppearanceBox, controller));
}

void ShowAccountAccentBox(
		not_null<Window::SessionController*> controller,
		not_null<Main::Account*> account) {
	if (account->sessionExists()) {
		controller->show(Box(AccountAccentBox, controller, &account->session()));
	}
}

AccountsStrip::AccountsStrip(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	bool compact)
: RpWidget(parent)
, _controller(controller)
, _compact(compact) {
	setMouseTracking(true);
	rpl::single(rpl::empty) | rpl::then(
		Core::App().domain().accountsChanges()
	) | rpl::on_next([=] {
		rebuild();
	}, lifetime());
	rpl::merge(
		Core::App().unreadBadgeChanges(),
		AyuSettings::getInstance().hideNotificationCountersValue(
		) | rpl::to_empty
	) | rpl::on_next([=] {
		update();
	}, lifetime());
}

void AccountsStrip::rebuild() {
	_entriesLifetime.destroy();
	_entries.clear();
	_over = _pressed = -1;
	const auto current = &_controller->session().account();
	for (const auto account : Core::App().domain().orderedAccounts()) {
		account->sessionChanges(
		) | rpl::on_next([=] {
			crl::on_main(this, [=] { rebuild(); });
		}, _entriesLifetime);
		if (account == current || !account->sessionExists()) {
			continue;
		}
		const auto session = &account->session();
		_entries.push_back({ .account = account });
		rpl::merge(
			session->downloaderTaskFinished(),
			session->changes().peerUpdates(
				session->user(),
				(Data::PeerUpdate::Flag::Name
					| Data::PeerUpdate::Flag::Photo)
			) | rpl::to_empty,
			AccountAppearance::AccentValue(
				session->uniqueId()
			) | rpl::skip(1) | rpl::to_empty
		) | rpl::on_next([=] {
			update();
		}, _entriesLifetime);
	}
	resizeToWidth(width());
	update();
}

int AccountsStrip::rowHeight() const {
	return _compact
		? st::vygramAccountRowHeightCompact
		: st::vygramAccountRowHeight;
}

int AccountsStrip::resizeGetHeight(int newWidth) {
	return _entries.empty()
		? 0
		: (st::vygramAccountDividerSkip + int(_entries.size()) * rowHeight());
}

int AccountsStrip::indexAt(QPoint position) const {
	const auto y = position.y() - st::vygramAccountDividerSkip;
	if (y < 0 || !rect().contains(position)) {
		return -1;
	}
	const auto index = y / rowHeight();
	return (index < int(_entries.size())) ? index : -1;
}

void AccountsStrip::setOver(int index) {
	if (_over == index) {
		return;
	}
	_over = index;
	setCursor((index >= 0) ? style::cur_pointer : style::cur_default);
	update();
}

void AccountsStrip::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto padding = st::vygramAccountPadding;
	const auto size = st::vygramAccountUserpic;
	const auto ring = st::vygramAccountRingWidth;
	const auto accentWidth = st::vygramAccountAccentWidth;
	const auto row = rowHeight();
	const auto wide = (width() >= st::vygramAccountWideRail);
	const auto hideCounters = AyuSettings::getInstance().hideNotificationCounters();
	p.fillRect(
		padding,
		(st::vygramAccountDividerSkip - st::lineWidth) / 2,
		width() - 2 * padding,
		st::lineWidth,
		st::shadowFg);
	auto top = st::vygramAccountDividerSkip;
	for (auto i = 0; i != int(_entries.size()); ++i) {
		auto &entry = _entries[i];
		const auto session = &entry.account->session();
		const auto user = session->user();
		if (i == _over) {
			p.fillRect(
				QRect(0, top, width(), row).adjusted(
					padding / 2,
					ring,
					-padding / 2,
					-ring),
				st::dialogsBgOver);
		}
		const auto accent = AccountAppearance::Accent(session->uniqueId());
		if (accent) {
			p.fillRect(0, top + row / 4, accentWidth, row / 2, *accent);
		}
		const auto x = wide ? padding : (width() - size) / 2;
		const auto y = top + (row - size) / 2;
		user->paintUserpicLeft(p, entry.view, x, y, width(), size);
		if (accent) {
			auto hq = PainterHighQualityEnabler(p);
			auto pen = QPen(*accent);
			pen.setWidthF(ring);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			AyuUserpic::PaintShape(p, QRectF(x, y, size, size).marginsAdded(
				{ ring / 2., ring / 2., ring / 2., ring / 2. }));
		}
		const auto unread = hideCounters
			? 0
			: session->data().unreadWithMentionsBadge();
		const auto badge = (unread > 0) ? QString::number(unread) : QString();
		p.setFont(st::normalFont);
		const auto badgeWidth = badge.isEmpty()
			? 0
			: (st::normalFont->width(badge) + padding / 2);
		if (wide) {
			const auto left = x + size + padding;
			const auto bounds = QRect(
				left,
				top,
				width() - left - padding - badgeWidth,
				row);
			p.setPen(st::windowFg);
			p.drawText(
				bounds,
				Qt::AlignVCenter | Qt::AlignLeft,
				st::normalFont->elided(user->name(), bounds.width()));
		}
		if (!badge.isEmpty()) {
			p.setPen(session->data().unreadWithMentionsBadgeMuted()
				? st::windowSubTextFg
				: st::windowActiveTextFg);
			const auto bounds = QRect(0, top, width(), row).adjusted(
				0,
				0,
				wide ? -padding : -ring,
				0);
			p.drawText(
				bounds,
				Qt::AlignRight | (wide ? Qt::AlignVCenter : Qt::AlignTop),
				badge);
		}
		top += row;
	}
}

void AccountsStrip::mouseMoveEvent(QMouseEvent *e) {
	setOver(indexAt(e->pos()));
}

void AccountsStrip::mousePressEvent(QMouseEvent *e) {
	const auto button = e->button();
	_pressed = (button == Qt::LeftButton || button == Qt::MiddleButton)
		? indexAt(e->pos())
		: -1;
}

void AccountsStrip::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed < 0 || pressed != indexAt(e->pos())) {
		return;
	}
	const auto newWindow = (e->button() == Qt::MiddleButton)
		|| e->modifiers().testFlag(Qt::ControlModifier);
	SwitchToAccount(_controller, _entries[pressed].account, newWindow);
}

void AccountsStrip::contextMenuEvent(QContextMenuEvent *e) {
	const auto index = indexAt(e->pos());
	if (index >= 0) {
		showContextMenu(index, e->globalPos());
	}
}

void AccountsStrip::leaveEventHook(QEvent *e) {
	setOver(-1);
}

void AccountsStrip::showContextMenu(int index, QPoint globalPosition) {
	const auto account = _entries[index].account;
	const auto controller = _controller;
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	const auto add = Ui::Menu::CreateAddActionCallback(_menu.get());
	add(tr::lng_menu_activate(tr::now), [=] {
		SwitchToAccount(controller, account, false);
	}, &st::menuIconProfile);
	add(tr::lng_context_new_window(tr::now), [=] {
		SwitchToAccount(controller, account, true);
	}, &st::menuIconNewWindow);
	add(tr::lng_vygram_account_accent(tr::now), [=] {
		ShowAccountAccentBox(controller, account);
	}, &st::menuIconPalette);
	_menu->popup(globalPosition);
}

} // namespace Ayu
