#include "ayu/ui/boxes/command_palette_box.h"

#include "ayu/data/ayu_database.h"
#include "ayu/ui/boxes/account_switcher.h"
#include "ayu/ui/boxes/chat_archive_box.h"
#include "ayu/ui/boxes/chat_labels_box.h"
#include "ayu/ui/boxes/protection_status_box.h"
#include "base/event_filter.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "settings/settings_common.h"
#include "settings/sections/settings_shortcuts.h"
#include "styles/style_menu_icons.h"
#include "ui/widgets/labels.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "info/downloads/info_downloads_widget.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_session_controller.h"
#include "window/window_filters_menu.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QKeyEvent>
#include <QTextEdit>

namespace Ayu {
namespace {

constexpr auto kVisibleRows = 8;

struct Entry {
	QString id;
	QString title;
	const style::icon *icon;
	std::optional<Shortcuts::Command> shortcut;
	Fn<void(QString)> open;
};

struct State {
	std::vector<Entry> entries;
	std::vector<int> matches;
	std::vector<Ui::SettingsButton*> buttons;
	QStringList recent;
	int selected = 0;
};

int Score(const QString &title, const QString &query) {
	if (query.isEmpty() || title.startsWith(query, Qt::CaseInsensitive)) {
		return 0;
	}
	if (title.contains(query, Qt::CaseInsensitive)) {
		return 1;
	}
	auto offset = qsizetype(0);
	for (const auto letter : query) {
		const auto at = title.indexOf(letter, offset, Qt::CaseInsensitive);
		if (at < 0) {
			return -1;
		}
		offset = at + 1;
	}
	return 2;
}

void Palette(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	session->account().sessionChanges() | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
	box->setTitle(tr::lng_vygram_command_palette());
	box->setWidth(st::boxWideWidth);
	const auto query = box->addRow(object_ptr<Ui::InputField>(
		box, st::defaultInputField, tr::lng_vygram_command_query()));
	box->setFocusCallback([=] { query->setFocus(); });
	const auto rows = box->addRow(object_ptr<Ui::VerticalLayout>(box), style::margins());
	const auto state = box->lifetime().make_state<State>();
	const auto historyKey = "vygram.palette." + std::to_string(session->uniqueId());
	const auto history = AyuDatabase::tryLoadPrivateSetting(historyKey);
	if (history && !history->isEmpty()) {
		state->recent = QString::fromUtf8(*history).split(QChar('|'));
	}
	using Command = Shortcuts::Command;
	state->entries = {
		{ u"search"_q, tr::lng_vygram_command_global_search(tr::now), &st::menuIconSearch, Command::Search, [=](QString text) {
			controller->searchMessages(text, {});
		} },
		{ u"saved"_q, tr::lng_saved_messages(tr::now), &st::menuIconSavedMessages, Command::ChatSelf, [=](QString) {
			controller->showPeerHistory(session->userId());
		} },
		{ u"settings"_q, tr::lng_menu_settings(tr::now), &st::menuIconSettings, Command::OpenSettings, [=](QString) {
			controller->showSettings();
		} },
		{ u"downloads"_q, tr::lng_downloads_section(tr::now), &st::menuIconDownload, Command::OpenDownloads, [=](QString) {
			controller->showSection(Info::Downloads::Make(session->user()));
		} },
		{ u"archive"_q, tr::lng_chat_archive_title(tr::now), &st::menuIconArchive, std::nullopt, [=](QString) {
			AyuUi::ShowChatArchive(controller);
		} },
		{ u"activity"_q, tr::lng_sr_chat_column_activity(tr::now), &st::menuIconNotifications, std::nullopt, [=](QString) {
			Window::ShowGraphiteActivity(controller);
		} },
		{ u"labels"_q, tr::lng_vygram_labels_title(tr::now), &st::menuIconTagFilter, std::nullopt, [=](QString) {
			ShowChatLabelFilter(controller);
		} },
		{ u"protection"_q, tr::lng_vygram_protection_title(tr::now), &st::menuIconLock, std::nullopt, [=](QString) {
			ShowProtectionStatus(controller);
		} },
		{ u"shortcuts"_q, tr::lng_vygram_shortcut_help(tr::now), &st::menuIconShortcut, Command::ShowShortcuts, [=](QString) {
			controller->showSettings(Settings::ShortcutsId());
		} },
	};
	auto accountIndex = 0;
	for (const auto account : Core::App().domain().orderedAccounts()) {
		const auto index = accountIndex++;
		if (!account->sessionExists() || account == &session->account()) {
			continue;
		}
		auto shortcut = std::optional<Command>();
		if (index < int(Shortcuts::kShowAccount.size())) {
			shortcut = *(Shortcuts::kShowAccount.begin() + index);
		}
		const auto other = &account->session();
		state->entries.push_back({
			u"account."_q + QString::number(other->uniqueId()),
			tr::lng_vygram_account_switch_to(
				tr::now,
				lt_name,
				other->user()->name()),
			&st::menuIconProfile,
			shortcut,
			[=](QString) { SwitchToAccount(controller, account); },
		});
	}
	state->entries.push_back({
		u"account_appearance"_q,
		tr::lng_vygram_account_appearance(tr::now),
		&st::menuIconPalette,
		std::nullopt,
		[=](QString) { ShowAccountAppearanceBox(controller); },
	});
	for (const auto row : *session->data().chatsList()->indexed()) {
		if (const auto chat = row->key().history()) {
			const auto peer = chat->peer;
			state->entries.push_back({
				QString::number(peer->id.value),
				peer->name(),
				&st::menuIconChatBubble,
				std::nullopt,
				[=, id = peer->id](QString) { controller->showPeerHistory(id); },
			});
		}
	}
	const auto execute = [=](int index) {
		const auto entry = state->entries[index];
		state->recent.removeAll(entry.id);
		state->recent.prepend(entry.id);
		while (state->recent.size() > kVisibleRows) {
			state->recent.removeLast();
		}
		if (!AyuDatabase::storePrivateSetting(historyKey, state->recent.join(QChar('|')).toUtf8())) {
			LOG(("Command palette: could not save recent commands"));
		}
		const auto text = query->getLastText().trimmed();
		box->closeBox();
		entry.open(text);
	};
	const auto highlight = [=] {
		for (auto i = 0; i < int(state->buttons.size()); ++i) {
			state->buttons[i]->setSynteticOver(i == state->selected);
		}
	};
	const auto bindings = Shortcuts::KeysCurrents();
	const auto shortcutLabel = [=](std::optional<Command> command) -> QString {
		if (command) {
			// Saved Messages also has a numeric alias. Prefer the letter shortcut.
			const auto saved = QKeySequence(u"Ctrl+G"_q);
			const auto i = bindings.find(saved);
			if (*command == Command::ChatSelf && i != bindings.end() && i->second.contains(*command)) {
				return saved.toString(QKeySequence::NativeText);
			}
			for (const auto &[sequence, commands] : bindings) {
				if (commands.contains(*command)) {
					return sequence.toString(QKeySequence::NativeText);
				}
			}
		}
		return {};
	};
	const auto refresh = [=] {
		const auto text = query->getLastText().trimmed();
		std::vector<std::pair<int, int>> ranked;
		for (auto i = 0; i < int(state->entries.size()); ++i) {
			const auto &entry = state->entries[i];
			const auto score = Score(entry.title, text);
			if (score < 0 && i != 0) {
				continue;
			}
			const auto recent = state->recent.indexOf(entry.id);
			const auto rank = (score < 0 ? 3 : score) * (kVisibleRows + 1)
				+ ((recent >= 0) ? int(recent) : kVisibleRows);
			ranked.emplace_back(rank, i);
		}
		const auto count = std::min(int(ranked.size()), kVisibleRows);
		std::partial_sort(ranked.begin(), ranked.begin() + count, ranked.end());
		state->buttons.clear();
		state->matches.clear();
		rows->clear();
		for (auto i = 0; i < count; ++i) {
			const auto index = ranked[i].second;
			const auto &entry = state->entries[index];
			const auto button = Settings::AddButtonWithLabel(
				rows, rpl::single(QString(entry.title)), rpl::single(shortcutLabel(entry.shortcut)),
				st::settingsButton, { entry.icon });
			button->setClickedCallback([=] { execute(index); });
			state->buttons.push_back(button);
			state->matches.push_back(index);
		}
		state->selected = 0;
		highlight();
	};
	query->changes() | rpl::on_next(refresh, box->lifetime());
	base::install_event_filter(box, query->rawTextEdit(), [=](not_null<QEvent*> event) {
		if (event->type() != QEvent::KeyPress || state->matches.empty()) {
			return base::EventFilterResult::Continue;
		}
		const auto key = static_cast<QKeyEvent*>(event.get())->key();
		const auto count = int(state->matches.size());
		if (key == Qt::Key_Up || key == Qt::Key_Down) {
			state->selected = (state->selected + (key == Qt::Key_Up ? -1 : 1) + count) % count;
			highlight();
		} else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
			execute(state->matches[state->selected]);
		} else if (key == Qt::Key_Escape) {
			box->closeBox();
		} else {
			return base::EventFilterResult::Continue;
		}
		return base::EventFilterResult::Cancel;
	});
	box->addRow(object_ptr<Ui::FlatLabel>(
		box, tr::ayu_GraphitePaletteKeys(), st::boxDividerLabel));
	refresh();
}

} // namespace

void ShowCommandPalette(not_null<Window::SessionController*> controller) {
	controller->show(Box(Palette, controller));
}

} // namespace Ayu
