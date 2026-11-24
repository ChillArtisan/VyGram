#include "ayu/ui/boxes/chat_archive_box.h"

#include "ayu/data/ayu_database.h"
#include "ayu/utils/ayu_mapper.h"
#include "base/base_file_utilities.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_session_controller.h"

#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QSaveFile>

#include <limits>

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace AyuUi {
namespace {

constexpr auto kPageSize = 30;
constexpr auto kMaxAttachmentBytes = 256LL * 1024 * 1024;
constexpr auto kPhotoMedia = 1;
constexpr auto kDocumentMedia = 2;

struct Filters {
	QString from;
	QString to;
	int minDate = 0;
	int maxDate = 0;
	int documentType = -1;
};

struct Attachment {
	ID id = 0;
	int type = 0;
	qint64 size = 0;
	QString name;
};

void BindAccount(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	controller->session().account().sessionChanges() | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
}

void AddText(
		not_null<Ui::VerticalLayout*> layout,
		rpl::producer<QString> text,
		bool selectable = false) {
	const auto label = layout->add(
		object_ptr<Ui::FlatLabel>(layout, std::move(text), st::boxLabel),
		st::boxRowPadding);
	label->setBreakEverywhere(true);
	label->setSelectable(selectable);
}

Ui::SettingsButton *AddAction(
		not_null<Ui::VerticalLayout*> layout,
		rpl::producer<QString> title,
		Fn<void()> callback) {
	const auto button = layout->add(object_ptr<Ui::SettingsButton>(
		layout,
		std::move(title),
		st::settingsButtonNoIcon));
	button->setClickedCallback(std::move(callback));
	return button;
}

Ui::InputField *AddInput(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> placeholder,
		const QString &text,
		int maxLength) {
	const auto input = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		std::move(placeholder),
		text));
	input->setMaxLength(maxLength);
	return input;
}

std::optional<int> DateBound(const QString &value, bool endOfDay) {
	if (value.isEmpty()) {
		return 0;
	}
	const auto date = QDate::fromString(value, Qt::ISODate);
	if (!date.isValid() || date.toString(Qt::ISODate) != value) {
		return std::nullopt;
	}
	const auto time = endOfDay ? date.endOfDay() : date.startOfDay();
	const auto seconds = time.toSecsSinceEpoch();
	if (!time.isValid() || seconds <= 0
		|| seconds > std::numeric_limits<int>::max()) {
		return std::nullopt;
	}
	return int(seconds);
}

void AddOption(
		not_null<Ui::GenericBox*> box,
		const std::shared_ptr<Ui::RadiobuttonGroup> &group,
		int value,
		rpl::producer<QString> title) {
	const auto option = box->addRow(object_ptr<Ui::Radiobutton>(
		box,
		group,
		value,
		QString(),
		st::defaultBoxCheckbox));
	option->setAllowTextLines();
	std::move(title) | rpl::on_next([=](const QString &text) {
		option->setText(text);
	}, option->lifetime());
}

void EditFilters(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Filters initial,
		Fn<void(Filters)> apply) {
	BindAccount(box, controller);
	box->setTitle(tr::lng_chat_archive_filters());
	const auto from = AddInput(box, tr::lng_chat_archive_date_from(), initial.from, 10);
	const auto to = AddInput(box, tr::lng_chat_archive_date_to(), initial.to, 10);
	const auto types = std::make_shared<Ui::RadiobuttonGroup>(initial.documentType);
	AddOption(box, types, -1, tr::lng_chat_archive_all_types());
	AddOption(box, types, 0, tr::lng_chat_archive_text());
	AddOption(box, types, kPhotoMedia, tr::lng_chat_archive_photo());
	AddOption(box, types, kDocumentMedia, tr::lng_chat_archive_document());
	box->addButton(tr::lng_settings_apply(), [=] {
		auto filters = Filters();
		filters.from = from->getLastText().trimmed();
		filters.to = to->getLastText().trimmed();
		const auto minDate = DateBound(filters.from, false);
		const auto maxDate = DateBound(filters.to, true);
		if (!minDate || !maxDate || (*minDate && *maxDate && *minDate > *maxDate)) {
			controller->showToast(tr::lng_chat_archive_invalid_date(tr::now));
			return;
		}
		filters.minDate = *minDate;
		filters.maxDate = *maxDate;
		filters.documentType = types->current();
		apply(std::move(filters));
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

[[nodiscard]] Attachment ReadAttachment(const AyuMessageBase &message) {
	const auto media = AyuMapper::deserializeMedia(message.documentSerialized);
	if (media.type() == mtpc_messageMediaPhoto) {
		const auto photo = media.c_messageMediaPhoto().vphoto();
		if (photo && photo->type() == mtpc_photo) {
			return { ID(photo->c_photo().vid().v), kPhotoMedia, 0, u"photo.jpg"_q };
		}
	}
	if (media.type() != mtpc_messageMediaDocument) {
		return {};
	}
	const auto document = media.c_messageMediaDocument().vdocument();
	if (!document || document->type() != mtpc_document) {
		return {};
	}
	const auto &data = document->c_document();
	const auto size = data.vsize().v;
	if (size <= 0 || size > kMaxAttachmentBytes) {
		return {};
	}
	auto name = QString();
	for (const auto &attribute : data.vattributes().v) {
		if (attribute.type() == mtpc_documentAttributeFilename) {
			name = base::FileNameFromUserString(QFileInfo(qs(
				attribute.c_documentAttributeFilename().vfile_name())).fileName());
			break;
		}
	}
	if (name.isEmpty() || name == u"."_q || name == u".."_q) {
		const auto mime = QMimeDatabase().mimeTypeForName(qs(data.vmime_type()));
		const auto suffix = mime.preferredSuffix();
		name = suffix.isEmpty() ? u"attachment"_q : u"attachment."_q + suffix;
	}
	return { ID(data.vid().v), kDocumentMedia, qint64(size), std::move(name) };
}

void SaveAttachment(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		ID userId,
		ID dialogId,
		Attachment attachment) {
	FileDialog::GetWritePath(
		box.get(),
		tr::lng_chat_archive_save_attachment(tr::now),
		FileDialog::AllFilesFilter(),
		attachment.name,
		crl::guard(box, [=](QString &&path) {
			if (path.isEmpty()) {
				return;
			}
			const auto bytes = AyuDatabase::loadArchivedChatMedia(
				userId,
				dialogId,
				attachment.id,
				attachment.type);
			if (bytes.isEmpty() || bytes.size() > kMaxAttachmentBytes
				|| (attachment.size && bytes.size() != attachment.size)) {
				controller->showToast(tr::lng_chat_archive_attachment_unavailable(tr::now));
				return;
			}
			auto file = QSaveFile(path);
			if (!file.open(QIODevice::WriteOnly)
				|| file.write(bytes) != bytes.size()
				|| !file.commit()) {
				controller->showToast(tr::lng_chat_archive_export_failed(tr::now));
			}
		}));
}

QString MessageDate(const AyuMessageBase &message) {
	return QLocale().toString(
		QDateTime::fromSecsSinceEpoch(message.date),
		QLocale::ShortFormat);
}

void ShowMessage(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		AyuMessageBase message) {
	BindAccount(box, controller);
	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::lng_chat_archive_open_message());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(MessageDate(message)),
		st::sessionDateLabel));
	if (!message.text.empty()) {
		AddText(box->verticalLayout(), rpl::single(QString::fromStdString(message.text)), true);
	}
	if (!message.documentSerialized.empty()) {
		const auto attachment = ReadAttachment(message);
		if (attachment.id) {
			AddText(box->verticalLayout(), rpl::single(attachment.name));
			AddAction(
				box->verticalLayout(),
				tr::lng_chat_archive_save_attachment(),
				[=, userId = message.userId, dialogId = message.dialogId] {
					SaveAttachment(box, controller, userId, dialogId, attachment);
				});
		} else {
			AddText(box->verticalLayout(), tr::lng_chat_archive_attachment_unavailable());
		}
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void ShowMessages(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		AyuDatabase::ArchivedChat chat,
		Fn<void()> changed) {
	struct State {
		Filters filters;
		QString search;
		QDate previousDay;
		ID before = 0;
		Fn<void()> load;
	};

	BindAccount(box, controller);
	box->setWidth(st::boxWideWidth);
	box->setTitle(rpl::single(QString::fromStdString(chat.name)));
	const auto state = box->lifetime().make_state<State>();
	const auto search = AddInput(box, tr::lng_chat_archive_search(), QString(), 256);
	const auto reset = [=] {
		state->search = search->getLastText().trimmed();
		state->before = 0;
		state->load();
	};
	search->submits() | rpl::on_next(reset, box->lifetime());
	AddAction(box->verticalLayout(), tr::lng_context_search_from(), reset);
	AddAction(box->verticalLayout(), tr::lng_chat_archive_filters(), [=] {
		controller->show(Box(EditFilters, controller, state->filters,
			crl::guard(box, [=](Filters filters) {
				state->filters = std::move(filters);
				reset();
			})), Ui::LayerOption::KeepOther);
	});
	const auto rows = box->addRow(object_ptr<Ui::VerticalLayout>(box), style::margins());
	const auto more = box->addRow(object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
		box,
		object_ptr<Ui::SettingsButton>(
			box,
			tr::lng_chat_archive_more(),
			st::settingsButtonNoIcon)),
		style::margins())->setDuration(0);
	more->entity()->setClickedCallback([=] {
		state->load();
	});
	state->load = [=] {
		const auto firstPage = !state->before;
		if (firstPage) {
			rows->clear();
			state->previousDay = QDate();
		}
		const auto &filters = state->filters;
		const auto messages = AyuDatabase::archivedChatMessages(
			chat.userId,
			chat.dialogId,
			state->before,
			kPageSize + 1,
			state->search.toStdString(),
			filters.minDate,
			filters.maxDate,
			filters.documentType);
		const auto count = std::min(int(messages.size()), kPageSize);
		if (!count && firstPage) {
			AddText(rows, AyuDatabase::isUnlocked()
				? tr::lng_chat_archive_no_results()
				: tr::lng_chat_archive_locked());
		}
		for (auto index = 0; index != count; ++index) {
			const auto &message = messages[index];
			const auto day = QDateTime::fromSecsSinceEpoch(message.date).date();
			if (day != state->previousDay) {
				state->previousDay = day;
				rows->add(object_ptr<Ui::FlatLabel>(
					rows,
					rpl::single(QLocale().toString(day, QLocale::LongFormat)),
					st::sessionDateLabel),
					st::boxRowPadding);
			}
			const auto text = QString::fromStdString(message.text).simplified();
			const auto preview = text.isEmpty()
				? tr::lng_chat_archive_attachment(tr::now)
				: text.left(160);
			const auto action = AddAction(rows, rpl::single(preview), [=] {
				controller->show(Box(ShowMessage, controller, message), Ui::LayerOption::KeepOther);
			});
			action->setAccessibleDescription(MessageDate(message));
		}
		if (count) {
			const auto &last = messages[count - 1];
			state->before = last.messageId;
		}
		more->toggle(int(messages.size()) > kPageSize, anim::type::instant);
	};
	state->load();
	box->addButton(tr::lng_chat_archive_purge(), [=] {
		controller->show(Ui::MakeConfirmBox({
			.text = tr::lng_chat_archive_purge_confirm(tr::now),
			.confirmed = Fn<void(Fn<void()>)>(crl::guard(box, [=](Fn<void()> close) {
				if (!AyuDatabase::purgeArchivedChat(chat.userId, chat.dialogId)) {
					controller->showToast(tr::lng_chat_archive_purge_failed(tr::now));
					return;
				}
				close();
				changed();
				box->closeBox();
			})),
			.confirmText = tr::lng_chat_archive_purge(tr::now),
			.cancelText = tr::lng_cancel(tr::now),
			.confirmStyle = &st::attentionBoxButton,
		}), Ui::LayerOption::KeepOther);
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void Archive(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	struct State {
		Fn<void()> load;
	};

	BindAccount(box, controller);
	box->setTitle(tr::lng_chat_archive_title());
	const auto userId = ID(controller->session().uniqueId());
	const auto state = box->lifetime().make_state<State>();
	const auto rows = box->addRow(object_ptr<Ui::VerticalLayout>(box), style::margins());
	state->load = [=] {
		rows->clear();
		if (!AyuDatabase::isUnlocked()) {
			AddText(rows, tr::lng_chat_archive_locked());
			return;
		}
		const auto chats = AyuDatabase::archivedChats(userId);
		if (chats.empty()) {
			AddText(rows, tr::lng_chat_archive_empty());
		}
		for (const auto &chat : chats) {
			AddAction(rows, rpl::single(QString::fromStdString(chat.name)), [=] {
				controller->show(Box(ShowMessages, controller, chat, crl::guard(box, [=] {
					state->load();
				})), Ui::LayerOption::KeepOther);
			});
		}
	};
	state->load();
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void ShowChatArchive(not_null<Window::SessionController*> controller) {
	controller->show(Box(Archive, controller));
}

} // namespace AyuUi
