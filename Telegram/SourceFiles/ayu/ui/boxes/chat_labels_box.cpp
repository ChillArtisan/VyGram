#include "ayu/ui/boxes/chat_labels_box.h"

#include "ayu/data/ayu_database.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace Ayu {
namespace {

void BindAccount(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	controller->session().account().sessionChanges() | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
}

std::string LabelsKey(not_null<Main::Session*> session) {
	return "vygram.chatLabels." + std::to_string(session->uniqueId());
}

QString PeerKey(PeerId peer) {
	return QString::number(peer.value);
}

std::optional<QJsonObject> LoadLabels(not_null<Main::Session*> session) {
	const auto bytes = AyuDatabase::tryLoadPrivateSetting(LabelsKey(session));
	if (!bytes) {
		return std::nullopt;
	}
	if (bytes->isEmpty()) {
		return QJsonObject();
	}
	const auto document = QJsonDocument::fromJson(*bytes);
	return document.isObject()
		? std::optional(document.object())
		: std::nullopt;
}

void EditLabel(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		PeerId peer) {
	const auto session = &controller->session();
	const auto labels = LoadLabels(session);
	if (!labels) {
		box->closeBox();
		controller->showToast(tr::lng_vygram_labels_error(tr::now));
		return;
	}
	BindAccount(box, controller);
	box->setTitle(tr::lng_vygram_labels_edit());
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_vygram_labels_name(),
		labels->value(PeerKey(peer)).toString()));
	field->setMaxLength(40);
	box->addButton(tr::lng_settings_save(), [=] {
		auto current = LoadLabels(session);
		if (current) {
			const auto label = field->getLastText().simplified();
			if (label.isEmpty()) {
				current->remove(PeerKey(peer));
			} else {
				current->insert(PeerKey(peer), label);
			}
			if (AyuDatabase::storePrivateSetting(
				LabelsKey(session),
				QJsonDocument(*current).toJson(QJsonDocument::Compact))) {
				box->closeBox();
				return;
			}
		}
		controller->showToast(tr::lng_vygram_labels_error(tr::now));
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void ShowLabelResults(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		QString selected) {
	BindAccount(box, controller);
	box->setWidth(st::boxWideWidth);
	box->setTitle(rpl::single(selected));
	const auto session = &controller->session();
	const auto labels = LoadLabels(session);
	if (!labels) {
		box->closeBox();
		controller->showToast(tr::lng_vygram_labels_error(tr::now));
		return;
	}
	const auto layout = box->verticalLayout();
	auto found = false;
	for (const auto row : *session->data().chatsList()->indexed()) {
		const auto history = row->key().history();
		if (!history) {
			continue;
		}
		const auto peer = history->peer;
		if (labels->value(PeerKey(peer->id)).toString() != selected) {
			continue;
		}
		found = true;
		layout->add(object_ptr<Ui::SettingsButton>(
			layout,
			rpl::single(peer->name()),
			st::settingsButtonNoIcon))->setClickedCallback([=] {
				box->closeBox();
				controller->showPeerHistory(peer);
			});
	}
	if (!found) {
		layout->add(object_ptr<Ui::FlatLabel>(
			layout, tr::lng_vygram_labels_empty_results(), st::boxLabel),
			st::boxRowPadding);
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void FilterLabels(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	BindAccount(box, controller);
	box->setTitle(tr::lng_vygram_labels_title());
	const auto labels = LoadLabels(&controller->session());
	if (!labels) {
		box->closeBox();
		controller->showToast(tr::lng_vygram_labels_error(tr::now));
		return;
	}
	auto names = QStringList();
	for (auto i = labels->begin(); i != labels->end(); ++i) {
		const auto label = i.value().toString();
		if (!label.isEmpty()) {
			names.push_back(label);
		}
	}
	names.removeDuplicates();
	names.sort(Qt::CaseInsensitive);
	const auto layout = box->verticalLayout();
	for (const auto &name : names) {
		layout->add(object_ptr<Ui::SettingsButton>(
			layout,
			rpl::single(name),
			st::settingsButtonNoIcon))->setClickedCallback([=] {
				controller->show(
					Box(ShowLabelResults, controller, name),
					Ui::LayerOption::KeepOther);
			});
	}
	if (names.isEmpty()) {
		layout->add(object_ptr<Ui::FlatLabel>(
			layout, tr::lng_vygram_labels_empty(), st::boxLabel),
			st::boxRowPadding);
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void ShowChatLabelEditor(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box(EditLabel, controller, peer->id));
}

void ShowChatLabelFilter(not_null<Window::SessionController*> controller) {
	controller->show(Box(FilterLabels, controller));
}

} // namespace Ayu
