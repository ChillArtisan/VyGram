#include "ayu/ui/boxes/chat_privacy_box.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/chat_privacy.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Ayu {
namespace {

using ChatPrivacy::IndicatorMode;

QString CurrentMode(
		not_null<PeerData*> peer,
		std::optional<IndicatorMode> mode) {
	if (!mode) {
		return tr::lng_vygram_chat_privacy_storage_error(tr::now);
	} else if (*mode == IndicatorMode::Allow) {
		return tr::lng_vygram_chat_privacy_current_on(tr::now);
	} else if (*mode == IndicatorMode::Hide) {
		return tr::lng_vygram_chat_privacy_current_off(tr::now);
	}
	return AyuSettings::ghost(&peer->session()).sendUploadProgress()
		? tr::lng_vygram_chat_privacy_inherited_on(tr::now)
		: tr::lng_vygram_chat_privacy_inherited_off(tr::now);
}

void ChatPrivacyBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	box->setTitle(tr::lng_vygram_chat_privacy_title());
	controller->session().account().sessionChanges() | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
	const auto layout = box->verticalLayout();
	const auto about = layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		tr::lng_vygram_chat_privacy_about(),
		st::boxLabel), st::boxRowPadding);
	about->setBreakEverywhere(true);

	const auto mode = ChatPrivacy::LoadIndicators(peer);
	const auto status = layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		CurrentMode(peer, mode),
		st::boxLabel), st::boxRowPadding);
	status->setBreakEverywhere(true);

	const auto addAction = [=](
			rpl::producer<QString> text,
			IndicatorMode selected) {
		const auto button = layout->add(object_ptr<Ui::SettingsButton>(
			layout,
			std::move(text),
			st::settingsButtonNoIcon));
		button->setClickedCallback([=] {
			if (!ChatPrivacy::SaveIndicators(peer, selected)) {
				controller->showToast(
					tr::lng_vygram_chat_privacy_storage_error(tr::now));
				return;
			}
			status->setText(CurrentMode(peer, selected));
		});
	};
	if (mode) {
		addAction(tr::lng_vygram_chat_privacy_default(), IndicatorMode::Inherit);
		addAction(tr::lng_vygram_chat_privacy_allow(), IndicatorMode::Allow);
		addAction(tr::lng_vygram_chat_privacy_block(), IndicatorMode::Hide);
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void ShowChatPrivacy(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box(ChatPrivacyBox, controller, peer));
}

} // namespace Ayu
