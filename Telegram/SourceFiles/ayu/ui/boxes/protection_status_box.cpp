#include "ayu/ui/boxes/protection_status_box.h"

#include "ayu/data/ayu_database.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "settings/sections/settings_local_passcode.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Ayu {
namespace {

void AddText(
		not_null<Ui::VerticalLayout*> layout,
		rpl::producer<QString> text) {
	const auto label = layout->add(
		object_ptr<Ui::FlatLabel>(layout, std::move(text), st::boxLabel),
		st::boxRowPadding);
	label->setBreakEverywhere(true);
}

void AddAction(
		not_null<Ui::VerticalLayout*> layout,
		rpl::producer<QString> text,
		Fn<void()> callback) {
	layout->add(object_ptr<Ui::SettingsButton>(
		layout,
		std::move(text),
		st::settingsButtonNoIcon))->setClickedCallback(std::move(callback));
}

void ProtectionStatusBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(tr::lng_vygram_protection_title());
	const auto layout = box->verticalLayout();
	AddText(layout, AyuDatabase::isUnlocked()
		? tr::lng_vygram_protection_archive_open()
		: tr::lng_vygram_protection_archive_closed());
	AddText(layout, tr::lng_vygram_protection_about());

	const auto hasPasscode = Core::App().domain().local().hasLocalPasscode();
	AddText(layout, hasPasscode
		? tr::lng_vygram_protection_passcode_on()
		: tr::lng_vygram_protection_passcode_off());
	if (hasPasscode) {
		AddAction(layout, tr::lng_vygram_protection_lock(), [=] {
			box->closeBox();
			Core::App().maybeLockByPasscode();
		});
		AddAction(layout, tr::lng_settings_passcode_title(), [=] {
			box->closeBox();
			controller->showSettings(Settings::LocalPasscodeManageId());
		});
	}
	AddAction(layout, tr::lng_vygram_protection_quit(), [=] {
		controller->show(Ui::MakeConfirmBox({
			.text = tr::lng_vygram_protection_quit_confirm(tr::now),
			.confirmed = [] { Core::Quit(); },
			.confirmText = tr::lng_vygram_protection_quit(tr::now),
			.cancelText = tr::lng_cancel(),
		}), Ui::LayerOption::KeepOther);
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void ShowProtectionStatus(not_null<Window::SessionController*> controller) {
	controller->show(Box(ProtectionStatusBox, controller));
}

} // namespace Ayu
