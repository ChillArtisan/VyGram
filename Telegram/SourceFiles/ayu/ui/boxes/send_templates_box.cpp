#include "ayu/ui/boxes/send_templates_box.h"

#include "api/api_send_progress.h"
#include "ayu/data/ayu_database.h"
#include "ayu/features/send_templates/send_templates.h"
#include "base/event_filter.h"
#include "base/weak_qptr.h"
#include "core/file_utilities.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_values.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/mtp_instance.h"
#include "storage/localimageloader.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"

#include <QFileInfo>
#include <QUuid>

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Ayu {
namespace {

using namespace SendTemplates;

constexpr auto kTickInterval = crl::time(250);
constexpr auto kRefreshInterval = crl::time(5000);
constexpr auto kActionTimeout = crl::time(10000);

rpl::producer<QString> TypeName(Type type) {
	switch (type) {
	case Type::Round: return tr::lng_send_templates_round();
	case Type::Voice: return tr::lng_send_templates_voice();
	case Type::Video: return tr::lng_send_templates_video();
	case Type::Photo: return tr::lng_send_templates_photo();
	}
	Unexpected("Invalid send template type.");
}

Api::SendProgressType ProgressType(Type type) {
	using Progress = Api::SendProgressType;
	switch (type) {
	case Type::Round: return Progress::RecordRound;
	case Type::Voice: return Progress::RecordVoice;
	case Type::Video: return Progress::RecordVideo;
	case Type::Photo: return Progress::UploadPhoto;
	}
	Unexpected("Invalid send template type.");
}

QString ErrorText(Error error, Type type) {
	if (error == Error::File) {
		return tr::lng_send_templates_file_error(tr::now);
	} else if (error == Error::Size) {
		return tr::lng_send_templates_size_error(tr::now);
	} else if (type == Type::Voice) {
		return tr::lng_send_templates_voice_error(tr::now);
	} else if (type == Type::Round) {
		return tr::lng_send_templates_round_error(tr::now);
	} else if (type == Type::Photo) {
		return tr::lng_send_templates_photo_error(tr::now);
	}
	return tr::lng_send_templates_video_error(tr::now);
}

void AddText(
		not_null<Ui::VerticalLayout*> layout,
		rpl::producer<QString> text) {
	const auto label = layout->add(
		object_ptr<Ui::FlatLabel>(layout, std::move(text), st::boxLabel),
		st::boxRowPadding);
	label->setBreakEverywhere(true);
}

Ui::SettingsButton *AddAction(
		not_null<Ui::VerticalLayout*> layout,
		rpl::producer<QString> text,
		Fn<void()> callback) {
	const auto button = layout->add(object_ptr<Ui::SettingsButton>(
		layout,
		std::move(text),
		st::settingsButtonNoIcon));
	button->setClickedCallback(std::move(callback));
	return button;
}

void BindContext(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Ui::RpWidget *context) {
	controller->session().account().sessionChanges() | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
	if (!context) {
		return;
	}
	QObject::connect(context, &QObject::destroyed, box, [=] {
		box->closeBox();
	});
	controller->activeChatEntryChanges() | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
}

void ChooseFile(
		not_null<Ui::GenericBox*> box,
		Fn<void(QString)> chosen) {
	const auto closed = box->lifetime().make_state<bool>(false);
	box->boxClosing() | rpl::on_next([=] { *closed = true; }, box->lifetime());
	FileDialog::GetOpenPath(
		box.get(),
		tr::lng_choose_files(tr::now),
		FileDialog::AllFilesFilter(),
		crl::guard(box, [=](FileDialog::OpenResult &&result) {
			if (!*closed && !result.paths.isEmpty()) {
				chosen(QFileInfo(result.paths.front()).absoluteFilePath());
			}
		}));
}

void Editor(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Entry entry,
		Fn<void()> changed) {
	struct State {
		Entry entry;
		rpl::variable<QString> path;
		bool checking = false;
		bool closed = false;
	};
	BindContext(box, controller, nullptr);
	const auto state = box->lifetime().make_state<State>();
	box->boxClosing() | rpl::on_next([=] {
		state->closed = true;
	}, box->lifetime());
	state->entry = entry;
	state->path = entry.path;
	if (state->entry.id.isEmpty()) {
		state->entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	}
	box->setTitle(tr::lng_send_templates_edit());
	const auto name = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_send_templates_name(),
		entry.name));
	name->setMaxLength(kMaxNameLength);
	const auto caption = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		tr::lng_send_templates_caption(),
		entry.caption));
	caption->setMaxLength(kMaxCaptionLength);
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(int(entry.type));
	for (const auto type : { Type::Round, Type::Voice, Type::Video, Type::Photo }) {
		const auto label = box->addRow(object_ptr<Ui::Radiobutton>(
			box,
			group,
			int(type),
			QString(),
			st::defaultBoxCheckbox));
		label->setAllowTextLines();
		TypeName(type) | rpl::on_next([=](const QString &text) {
			label->setText(text);
		}, label->lifetime());
	}
	AddText(box->verticalLayout(), tr::lng_send_templates_local_only());
	AddText(box->verticalLayout(), state->path.value());
	AddAction(box->verticalLayout(), tr::lng_send_templates_replace(), [=] {
		ChooseFile(box, [=](QString path) { state->path = std::move(path); });
	});
	box->addButton(tr::lng_settings_save(), [=] {
		if (state->checking || state->closed) {
			return;
		}
		state->entry.name = name->getLastText().trimmed();
		state->entry.caption = caption->getLastText();
		state->entry.path = state->path.current();
		state->entry.type = Type(group->current());
		if (state->entry.name.isEmpty()) {
			name->showError();
			return;
		} else if (state->entry.path.isEmpty()) {
			controller->showToast(tr::lng_send_templates_file_error(tr::now));
			return;
		}
		state->checking = true;
		const auto snapshot = state->entry;
		const auto weak = base::make_weak(box);
		crl::async([=] {
			const auto prepared = Prepare(snapshot);
			const auto error = prepared.error;
			crl::on_main(weak, [=] {
				if (state->closed) {
					return;
				}
				state->checking = false;
				if (error != Error::None) {
					controller->showToast(ErrorText(error, snapshot.type));
				} else if (!Put(controller->session().uniqueId(), snapshot)) {
					controller->showToast(tr::lng_send_templates_storage_error(tr::now));
				} else {
					box->closeBox();
					changed();
				}
			});
		});
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] { name->setFocusFast(); });
}

class Run final {
public:
	Run(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Entry entry,
		Api::SendAction action);
	~Run();

	void prepare();
	void start(int seconds);
	void stop();
	[[nodiscard]] rpl::producer<QString> status() const;
	[[nodiscard]] rpl::producer<bool> canStart() const;

private:
	[[nodiscard]] bool allowed();
	void checkPayment();
	void begin();
	void refresh();
	void tick();
	void finish();
	void fail(QString text);

	enum class Phase { Preparing, Ready, Payment, Running, Finished };
	const not_null<Ui::GenericBox*> _box;
	const not_null<Window::SessionController*> _controller;
	const base::weak_ptr<Main::Session> _session;
	const Entry _entry;
	Api::SendAction _action;
	std::shared_ptr<Prepared> _prepared;
	SendPaymentHelper _payment;
	base::Timer _timer;
	rpl::variable<QString> _status;
	rpl::variable<bool> _canStart = false;
	rpl::lifetime _runningLifetime;
	mtpRequestId _requestId = 0;
	crl::time _deadline = 0;
	crl::time _nextRefresh = 0;
	crl::time _requestStarted = 0;
	int _seconds = 0;
	Phase _phase = Phase::Preparing;

};

base::flat_map<Main::Session*, Run*> ActiveRuns;

Run::Run(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	Entry entry,
	Api::SendAction action)
: _box(box)
, _controller(controller)
, _session(base::make_weak(&controller->session()))
, _entry(std::move(entry))
, _action(std::move(action))
, _timer([=] { tick(); })
, _status(tr::lng_send_templates_checking(tr::now)) {
	_action.clearDraft = false;
	_box->boxClosing() | rpl::on_next([=] { stop(); }, _box->lifetime());
	base::install_event_filter(_box, _box->window(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close) {
			stop();
			_box->closeBox();
		}
		return base::EventFilterResult::Continue;
	});
}

Run::~Run() {
	stop();
}

void Run::prepare() {
	const auto weak = base::make_weak(_box);
	const auto entry = _entry;
	crl::async([=] {
		const auto prepared = std::make_shared<Prepared>(Prepare(entry));
		crl::on_main(weak, [=] {
			if (_phase != Phase::Preparing) {
				return;
			} else if (prepared->error != Error::None) {
				fail(ErrorText(prepared->error, entry.type));
				return;
			}
			_prepared = prepared;
			_phase = Phase::Ready;
			_canStart = true;
			_status = tr::lng_send_templates_ready(tr::now);
		});
	});
}

bool Run::allowed() {
	if (!_session || !AyuDatabase::isUnlocked()) {
		fail(tr::lng_send_templates_storage_error(tr::now));
		return false;
	} else if (!Unchanged(_entry, *_prepared)) {
		fail(tr::lng_send_templates_file_error(tr::now));
		return false;
	} else if (_session->mtp().dcstate() != MTP::ConnectedState) {
		fail(tr::lng_send_templates_network_error(tr::now));
		return false;
	}
	const auto history = _action.history;
	const auto peer = history->peer;
	const auto restriction = (_entry.type == Type::Round)
		? ChatRestriction::SendVideoMessages
		: (_entry.type == Type::Voice)
		? ChatRestriction::SendVoiceMessages
		: (_entry.type == Type::Video)
		? ChatRestriction::SendVideos
		: ChatRestriction::SendPhotos;
	const auto forum = peer->forum();
	const auto topic = forum
		? forum->topicFor(_action.replyTo.topicRootId)
		: nullptr;
	const auto thread = topic
		? not_null<Data::Thread*>(topic)
		: not_null<Data::Thread*>(history);
	if ((forum && !topic)
		|| !Data::CanSend(thread, restriction, false)
		|| _action.options.scheduled
		|| _action.options.shortcutId
		|| _action.replyTo.monoforumPeerId) {
		fail(tr::lng_send_templates_restricted(tr::now));
		return false;
	} else if (!_session->sendProgressManager().canSendPrepared(history)) {
		fail(tr::lng_send_templates_status_unavailable(tr::now));
		return false;
	}
	if (const auto error = GetErrorForSending(peer, {
			.topicRootId = _action.replyTo.topicRootId,
			.messagesCount = 1,
		})) {
		fail(error.text);
		return false;
	}
	return true;
}

void Run::start(int seconds) {
	if (_phase != Phase::Ready && _phase != Phase::Payment) {
		return;
	} else if (seconds < kMinDelay || seconds > kMaxDelay) {
		_controller->showToast(tr::lng_send_templates_delay_error(tr::now));
		return;
	} else if (!allowed()) {
		return;
	}
	_seconds = seconds;
	_phase = Phase::Payment;
	_status = tr::lng_send_templates_payment(tr::now);
	checkPayment();
}

void Run::checkPayment() {
	if (_phase != Phase::Payment || !allowed()) {
		return;
	}
	if (!_payment.check(
			_controller,
			_action.history->peer,
			_action.options,
			1,
			crl::guard(_box, [=](int stars) {
				if (_phase == Phase::Payment) {
					_action.options.starsApproved = stars;
					checkPayment();
				}
			}))) {
		return;
	}
	begin();
}

void Run::begin() {
	if (_phase != Phase::Payment || !allowed()) {
		return;
	}
	const auto session = _session.get();
	if (ActiveRuns.contains(session)) {
		fail(tr::lng_send_templates_busy(tr::now));
		return;
	}
	ActiveRuns.emplace(session, this);
	_canStart = false;
	_phase = Phase::Running;
	_deadline = 0;
	_session->sendProgressManager().ordinaryActivity() | rpl::on_next([=](
			const std::pair<not_null<History*>, MsgId> &activity) {
		if (activity.first == _action.history
			&& activity.second == _action.replyTo.topicRootId) {
			stop();
			_box->closeBox();
		}
	}, _runningLifetime);
	refresh();
	if (_phase == Phase::Running) {
		_timer.callEach(kTickInterval);
		tick();
	}
}

void Run::refresh() {
	_requestStarted = crl::now();
	_nextRefresh = _requestStarted + kRefreshInterval;
	_requestId = _session->sendProgressManager().updatePrepared(
		_action.history,
		_action.replyTo.topicRootId,
		ProgressType(_entry.type),
		crl::guard(_box, [=](bool success, bool showError) {
			_requestId = 0;
			if (_phase != Phase::Running) {
				return;
			} else if (!success) {
				fail(showError
					? tr::lng_send_templates_network_error(tr::now)
					: QString());
				return;
			}
			if (!_deadline) {
				_deadline = crl::now() + _seconds * crl::time(1000);
			}
			if (crl::now() >= _deadline) {
				finish();
			}
		}));
}

void Run::tick() {
	if (_phase != Phase::Running || !allowed()) {
		return;
	}
	const auto now = crl::now();
	if (_requestId && now - _requestStarted >= kActionTimeout) {
		fail(tr::lng_send_templates_network_error(tr::now));
		return;
	} else if (_deadline && now >= _deadline && !_requestId) {
		finish();
		return;
	} else if (now >= _nextRefresh && !_requestId) {
		refresh();
	}
	if (!_deadline) {
		_status = tr::lng_send_templates_connecting(tr::now);
		return;
	}
	_status = tr::lng_send_templates_running(
		tr::now,
		lt_duration,
		QString::number(std::max(crl::time(0), (_deadline - now + 999) / 1000)));
}

void Run::finish() {
	if (_phase != Phase::Running || !allowed()) {
		return;
	}
	if (_entry.type == Type::Round && !RefreshRoundMetadata(*_prepared)) {
		fail(ErrorText(Error::Format, _entry.type));
		return;
	}
	stop();
	if (_entry.type == Type::Round || _entry.type == Type::Voice) {
		_session->api().sendVoiceMessage(
			_prepared->bytes,
			VoiceWaveform(),
			_prepared->duration,
			_entry.type == Type::Round,
			_action,
			TextWithTags{ _entry.caption });
	} else {
		_session->api().sendFiles(
			std::move(_prepared->list),
			SendMediaType::Photo,
			nullptr,
			_action);
	}
	_box->closeBox();
}

void Run::stop() {
	const auto running = (_phase == Phase::Running);
	const auto finished = (_phase == Phase::Finished);
	_phase = Phase::Finished;
	if (!finished) {
		_status = tr::lng_send_templates_canceled(tr::now);
	}
	_canStart = false;
	_timer.cancel();
	_payment.clear();
	_runningLifetime.destroy();
	for (auto i = ActiveRuns.begin(); i != ActiveRuns.end(); ++i) {
		if (i->second == this) {
			ActiveRuns.erase(i);
			break;
		}
	}
	if (running && _session) {
		_session->sendProgressManager().cancelPrepared(
			_action.history,
			_action.replyTo.topicRootId,
			base::take(_requestId));
	}
}

void Run::fail(QString text) {
	stop();
	_status = std::move(text);
}

rpl::producer<QString> Run::status() const {
	return _status.value();
}

rpl::producer<bool> Run::canStart() const {
	return _canStart.value();
}

void Preview(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Ui::RpWidget *context,
		Fn<Api::SendAction()> actionFactory,
		Entry entry,
		Fn<void()> changed) {
	box->setTitle(rpl::single(entry.name));
	BindContext(box, controller, context);
	AddText(box->verticalLayout(), TypeName(entry.type));
	AddText(box->verticalLayout(), rpl::single(QFileInfo(entry.path).fileName()));
	AddText(box->verticalLayout(), entry.caption.isEmpty()
		? tr::lng_send_templates_no_caption()
		: rpl::single(entry.caption));
	auto stopRun = Fn<void()>();
	if (actionFactory) {
		AddText(box->verticalLayout(), tr::lng_send_templates_delay());
		const auto delay = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_send_templates_seconds(),
			u"5"_q));
		delay->setMaxLength(2);
		const auto run = box->lifetime().make_state<Run>(
			box,
			controller,
			entry,
			actionFactory());
		stopRun = [=] { run->stop(); };
		AddText(box->verticalLayout(), run->status());
		run->prepare();
		const auto start = box->addButton(tr::lng_send_templates_start(), [=] {
			auto ok = false;
			const auto seconds = delay->getLastText().toInt(&ok);
			if (!ok || seconds < kMinDelay || seconds > kMaxDelay) {
				delay->showError();
				return;
			}
			run->start(seconds);
		});
		run->canStart() | rpl::on_next([=](bool ready) {
			start->setDisabled(!ready);
			delay->setDisabled(!ready);
		}, box->lifetime());
	}
	AddAction(box->verticalLayout(), tr::lng_send_templates_edit(), [=] {
		box->closeBox();
		controller->show(
			Box(Editor, controller, entry, changed),
			Ui::LayerOption::KeepOther);
	});
	AddAction(box->verticalLayout(), tr::lng_send_templates_replace(), [=] {
		if (stopRun) {
			stopRun();
		}
		ChooseFile(box, [=](QString path) {
			auto copy = entry;
			copy.path = std::move(path);
			box->closeBox();
			controller->show(
				Box(Editor, controller, copy, changed),
				Ui::LayerOption::KeepOther);
		});
	});
	AddAction(box->verticalLayout(), tr::lng_send_templates_delete(), [=] {
		if (Remove(controller->session().uniqueId(), entry.id)) {
			changed();
			box->closeBox();
		} else {
			controller->showToast(tr::lng_send_templates_storage_error(tr::now));
		}
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void Library(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Ui::RpWidget *context,
		Fn<Api::SendAction()> actionFactory) {
	box->setTitle(tr::lng_send_templates_title());
	BindContext(box, controller, context);
	AddText(box->verticalLayout(), tr::lng_send_templates_local_only());
	struct State {
		Fn<void()> rebuild;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto rows = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());
	state->rebuild = crl::guard(box, [=] {
		rows->clear();
		const auto entries = Load(controller->session().uniqueId());
		if (!entries) {
			AddText(rows, tr::lng_send_templates_storage_error());
			return;
		} else if (entries->empty()) {
			AddText(rows, tr::lng_send_templates_empty());
			return;
		}
		for (const auto &entry : *entries) {
			AddAction(rows, rpl::single(entry.name), [=] {
				const auto changed = crl::guard(box, [=] {
					state->rebuild();
				});
				controller->show(
					Box(Preview, controller, context, actionFactory, entry, changed),
					Ui::LayerOption::KeepOther);
			});
		}
	});
	state->rebuild();
	box->addButton(tr::lng_send_templates_create(), [=] {
		const auto changed = crl::guard(box, [=] {
			state->rebuild();
		});
		controller->show(
			Box(Editor, controller, Entry(), changed),
			Ui::LayerOption::KeepOther);
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void ShowSendTemplates(
		not_null<Window::SessionController*> controller,
		Ui::RpWidget *context,
		Fn<Api::SendAction()> actionFactory) {
	if (actionFactory) {
		const auto action = actionFactory();
		actionFactory = [=] { return action; };
	}
	controller->show(Box(Library, controller, context, actionFactory));
}

} // namespace Ayu
