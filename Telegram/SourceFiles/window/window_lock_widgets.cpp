/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_lock_widgets.h"

#include "lang/lang_keys.h"
#include "storage/storage_domain.h"
#include "mainwindow.h"
#include "core/application.h"
#include "api/api_text_entities.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_session_controller.h"
#include "main/main_domain.h"
#include "styles/style_layers.h"
#include "styles/style_passcode_box.h"
#include "styles/style_window_lock_widgets.h"

// AyuGram includes
#include "ayu/security/password_policy.h"
#include "base/event_filter.h"


namespace Window {

Fn<void()> AddLocalPasscodeVisibilityToggle(
		not_null<Ui::PasswordInput*> field) {
	const auto button = Ui::CreateChild<Ui::IconButton>(
		field.get(), st::passcodeRevealButton);
	button->setFocusPolicy(Qt::TabFocus);
	QWidget::setTabOrder(field, button);
	const auto reserve = std::max(0,
		button->width() - field->contentsMargins().right());
	field->QLineEdit::setTextMargins(0, 0, reserve, 0);
	const auto setVisible = [=](bool visible) {
		const auto cursor = field->cursorPosition();
		field->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
		field->setCursorPosition(cursor);
		button->setIconOverride(visible
			? &st::passcodeRevealActive : nullptr);
		const auto text = visible
			? tr::lng_gift_menu_hide(tr::now)
			: tr::lng_gift_menu_show(tr::now);
		button->setAccessibleName(text);
		button->setToolTip(text);
		button->accessibilityNameChanged();
	};
	const auto hide = [=] { setVisible(false); };
	hide();
	button->setClickedCallback([=] {
		const auto visible = field->echoMode() != QLineEdit::Normal;
		if (!button->hasFocus()) {
			field->setFocusFast();
		}
		setVisible(visible);
	});
	base::install_event_filter(button, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::FocusIn) {
			button->setSynteticOver(true);
		} else if (event->type() == QEvent::FocusOut) {
			button->setSynteticOver(false);
			hide();
		} else if (event->type() == QEvent::Hide) {
			hide();
		}
		return base::EventFilterResult::Continue;
	});
	field->sizeValue() | rpl::on_next([=](QSize size) {
		button->moveToRight(0, size.height() - button->height());
	}, button->lifetime());
	QObject::connect(field, &Ui::MaskedInputField::blurred, field, hide);
	QObject::connect(field, &Ui::MaskedInputField::cancelled, field, hide);
	rpl::merge(field->shownValue(), field->windowActiveValue()
	) | rpl::on_next([=](bool active) {
		if (!active) {
			hide();
		}
	}, field->lifetime());
	return hide;
}

bool IsStrongLocalPasscode(const QString &passcode) {
	for (auto i = 0; i != passcode.size(); ++i) {
		if (passcode[i].isHighSurrogate()) {
			if (++i == passcode.size() || !passcode[i].isLowSurrogate()) {
				return false;
			}
		} else if (passcode[i].isLowSurrogate()) {
			return false;
		}
	}
	return Ayu::Security::IsStrongLocalPassword(passcode.toStdU32String());
}

PasscodeAttempt TryPasscode(const QString &passcode) {
	if (passcode.isEmpty()) {
		return PasscodeAttempt::Empty;
	} else if (!passcodeCanTry()) {
		return PasscodeAttempt::Flood;
	}
	const auto utf8 = passcode.toUtf8();
	auto &domain = Core::App().domain();
	auto correct = false;
	if (domain.started()) {
		correct = domain.local().checkPasscode(utf8);
	} else {
		const auto result = domain.start(utf8);
		if (result == Storage::StartResult::StorageError) {
			return PasscodeAttempt::StorageError;
		}
		correct = (result == Storage::StartResult::Success)
			|| (result == Storage::StartResult::NeedsPasscodeSetup);
	}
	if (!correct) {
		cSetPasscodeBadTries(cPasscodeBadTries() + 1);
		cSetPasscodeLastTry(crl::now());
		return PasscodeAttempt::Wrong;
	}
	return PasscodeAttempt::Correct;
}

LockWidget::LockWidget(QWidget *parent, not_null<Controller*> window)
: RpWidget(parent)
, _window(window) {
	show();
}

LockWidget::~LockWidget() = default;

not_null<Controller*> LockWidget::window() const {
	return _window;
}

void LockWidget::setInnerFocus() {
	setFocus();
}

void LockWidget::showAnimated(QPixmap oldContentCache) {
	_showAnimation = nullptr;

	showChildren();
	setInnerFocus();
	auto newContentCache = Ui::GrabWidget(this);
	hideChildren();

	_showAnimation = std::make_unique<Window::SlideAnimation>();
	_showAnimation->setRepaintCallback([=] { update(); });
	_showAnimation->setFinishedCallback([=] { showFinished(); });
	_showAnimation->setPixmaps(oldContentCache, newContentCache);
	_showAnimation->start();

	show();
}

void LockWidget::showFinished() {
	showChildren();
	_window->widget()->setInnerFocus();
	_showAnimation = nullptr;
	if (const auto controller = _window->sessionController()) {
		controller->clearSectionStack();
	}
}

void LockWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	if (_showAnimation) {
		_showAnimation->paintContents(p);
		return;
	}
	paintContent(p);
}

void LockWidget::paintContent(QPainter &p) {
	p.fillRect(rect(), st::windowBg);
}

PasscodeLockWidget::PasscodeLockWidget(
	QWidget *parent,
	not_null<Controller*> window)
: LockWidget(parent, window)
, _setup(Core::App().domain().local().needsPasscodeSetup()
	|| (Core::App().domain().started()
		&& (!Core::App().domain().local().hasLocalPasscode()
			|| Core::App().domain().local().requiresPasscodeUpgrade())))
, _title(this, _setup
	? tr::lng_terminal_password_create_title()
	: tr::lng_terminal_password_unlock_title(), st::passcodeTitle)
, _about(this, _setup
	? tr::lng_terminal_password_setup_about()
	: tr::lng_terminal_password_unlock_about(), st::passcodeAbout)
, _passcode(this, st::passcodeInput, _setup
	? tr::lng_terminal_password_new()
	: tr::lng_passcode_ph())
, _confirm(_setup
	? object_ptr<Ui::PasswordInput>(this,
		st::passcodeInput, tr::lng_terminal_password_confirm())
	: nullptr)
, _errorLabel(this, QString(), st::passcodeError)
, _submit(this, _setup
	? tr::lng_terminal_password_create()
	: tr::lng_passcode_submit(), st::passcodeSubmit) {
	_hidePasscode = AddLocalPasscodeVisibilityToggle(_passcode);
	if (_confirm) {
		_hideConfirm = AddLocalPasscodeVisibilityToggle(_confirm);
	}
	connect(_passcode, &Ui::MaskedInputField::changed, [=] { changed(); });
	connect(_passcode, &Ui::MaskedInputField::submitted, [=] {
		if (_confirm) {
			_confirm->setFocusFast();
		} else {
			submit();
		}
	});
	if (_confirm) {
		connect(_confirm, &Ui::MaskedInputField::changed, [=] { changed(); });
		connect(_confirm, &Ui::MaskedInputField::submitted, [=] { submit(); });
	}
	_submit->setClickedCallback([=] { submit(); });
}

void PasscodeLockWidget::submit() {
	_hidePasscode();
	if (_hideConfirm) {
		_hideConfirm();
	}
	if (_setup) {
		if (!IsStrongLocalPasscode(_passcode->text())) {
			_error = tr::lng_terminal_password_weak(tr::now);
			_passcode->showError();
			_passcode->setFocusFast();
		} else if (_passcode->text() != _confirm->text()) {
			_error = tr::lng_passcode_differ(tr::now);
			_confirm->showError();
			_confirm->selectAll();
			_confirm->setFocusFast();
		} else {
			auto &domain = Core::App().domain();
			const auto utf8 = _passcode->text().toUtf8();
			if (!domain.local().setPasscode(utf8)
				|| (!domain.started()
					&& domain.start(utf8) != Storage::StartResult::Success)) {
				_error = tr::lng_terminal_archive_error(tr::now);
			} else {
				cSetPasscodeBadTries(0);
				Core::App().localPasscodeChanged();
				Core::App().unlockPasscode();
				return;
			}
		}
		_errorLabel->setText(_error);
		resizeEvent(nullptr);
		return;
	}
	switch (TryPasscode(_passcode->text())) {
	case PasscodeAttempt::Empty:
		_passcode->showError();
		return;
	case PasscodeAttempt::Flood:
		_error = tr::lng_flood_error(tr::now);
		break;
	case PasscodeAttempt::StorageError:
		_error = tr::lng_terminal_archive_error(tr::now);
		break;
	case PasscodeAttempt::Wrong:
		error();
		return;
	case PasscodeAttempt::Correct:
		Core::App().unlockPasscode();
		return;
	}
	_errorLabel->setText(_error);
	_passcode->showError();
	resizeEvent(nullptr);
}

void PasscodeLockWidget::error() {
	_error = tr::lng_passcode_wrong(tr::now);
	_errorLabel->setText(_error);
	_passcode->selectAll();
	_passcode->showError();
	resizeEvent(nullptr);
}

void PasscodeLockWidget::changed() {
	if (!_error.isEmpty()) {
		_error.clear();
		_errorLabel->setText(QString());
		resizeEvent(nullptr);
	}
}

void PasscodeLockWidget::resizeEvent(QResizeEvent *e) {
	const auto fieldWidth = std::min(st::passcodeInput.width,
		std::max(0, width() - 2 * st::passcodePagePadding));
	const auto left = (width() - fieldWidth) / 2;
	_title->resizeToWidth(fieldWidth);
	_about->resizeToWidth(fieldWidth);
	_errorLabel->resizeToWidth(fieldWidth);
	_passcode->resize(fieldWidth, _passcode->height());
	if (_confirm) {
		_confirm->resize(fieldWidth, _confirm->height());
	}
	const auto fieldsHeight = _passcode->height() + (_confirm
		? (st::passcodeFieldSkip + _confirm->height()) : 0);
	const auto submitSkip = std::max(st::passcodeSubmitSkip,
		st::passcodeErrorSkip + _errorLabel->height()
			+ st::passcodeErrorSkip);
	const auto totalHeight = _title->height() + st::passcodeTitleSkip
		+ _about->height() + st::passcodeArchiveAboutSkip + fieldsHeight
		+ submitSkip + _submit->height();
	auto top = std::max(st::passcodePagePadding,
		(height() - totalHeight) / 2);
	_title->moveToLeft(left, top);
	top += _title->height() + st::passcodeTitleSkip;
	_about->moveToLeft(left, top);
	top += _about->height() + st::passcodeArchiveAboutSkip;
	_passcode->moveToLeft(left, top);
	top += _passcode->height();
	if (_confirm) {
		top += st::passcodeFieldSkip;
		_confirm->moveToLeft(left, top);
		top += _confirm->height();
	}
	_errorLabel->moveToLeft(left, top + st::passcodeErrorSkip);
	_submit->setFullWidth(fieldWidth);
	_submit->moveToLeft(left, top + submitSkip);

}

void PasscodeLockWidget::setInnerFocus() {
	LockWidget::setInnerFocus();
	_passcode->setFocusFast();
}

TermsLock TermsLock::FromMTP(
		Main::Session *session,
		const MTPDhelp_termsOfService &data) {
	const auto minAge = data.vmin_age_confirm();
	return {
		bytes::make_vector(data.vid().c_dataJSON().vdata().v),
		TextWithEntities {
			qs(data.vtext()),
			Api::EntitiesFromMTP(session, data.ventities().v) },
		(minAge ? std::make_optional(minAge->v) : std::nullopt),
		data.is_popup()
	};
}

TermsBox::TermsBox(
	QWidget*,
	const TermsLock &data,
	rpl::producer<QString> agree,
	rpl::producer<QString> cancel)
: _data(data)
, _agree(std::move(agree))
, _cancel(std::move(cancel)) {
}

TermsBox::TermsBox(
	QWidget*,
	const TextWithEntities &text,
	rpl::producer<QString> agree,
	rpl::producer<QString> cancel,
	bool attentionAgree)
: _data{ {}, text, std::nullopt, false }
, _agree(std::move(agree))
, _cancel(std::move(cancel))
, _attentionAgree(attentionAgree) {
}

rpl::producer<> TermsBox::agreeClicks() const {
	return _agreeClicks.events();
}

rpl::producer<> TermsBox::cancelClicks() const {
	return _cancelClicks.events();
}

void TermsBox::prepare() {
	setTitle(tr::lng_terms_header());

	auto check = std::make_unique<Ui::CheckView>(st::defaultCheck, false);
	const auto ageCheck = check.get();
	const auto age = _data.minAge
		? Ui::CreateChild<Ui::PaddingWrap<Ui::Checkbox>>(
			this,
			object_ptr<Ui::Checkbox>(
				this,
				tr::lng_terms_age(tr::now, lt_count, *_data.minAge),
				st::defaultCheckbox,
				std::move(check)),
			st::termsAgePadding)
		: nullptr;
	if (age) {
		age->resizeToNaturalWidth(st::boxWideWidth);
	}

	const auto content = setInnerWidget(
		object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			this,
			object_ptr<Ui::FlatLabel> (
				this,
				rpl::single(_data.text),
				st::termsContent),
			st::termsPadding),
		0,
		age ? age->height() : 0);
	const auto show = uiShow();
	content->entity()->setClickHandlerFilter([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton button) {
		const auto link = handler
			? handler->copyToClipboardText()
			: QString();
		if (TextUtilities::RegExpMention().match(link).hasMatch()) {
			_lastClickedMention = link;
			show->showToast(
				tr::lng_terms_agree_to_proceed(tr::now, lt_bot, link));
			return false;
		}
		return true;
	});

	const auto errorAnimationCallback = [=] {
		const auto check = ageCheck;
		const auto error = _ageErrorAnimation.value(
			_ageErrorShown ? 1. : 0.);
		if (error == 0.) {
			check->setUntoggledOverride(std::nullopt);
		} else {
			const auto color = anim::color(
				st::defaultCheck.untoggledFg,
				st::boxTextFgError,
				error);
			check->setUntoggledOverride(color);
		}
	};
	const auto toggleAgeError = [=](bool shown) {
		if (_ageErrorShown != shown) {
			_ageErrorShown = shown;
			_ageErrorAnimation.start(
				[=] { errorAnimationCallback(); },
				_ageErrorShown ? 0. : 1.,
				_ageErrorShown ? 1. : 0.,
				st::defaultCheck.duration);
		}
	};

	const auto &agreeStyle = _attentionAgree
		? st::attentionBoxButton
		: st::defaultBoxButton;
	addButton(std::move(_agree), [=] {}, agreeStyle)->clicks(
	) | rpl::filter([=] {
		if (age && !age->entity()->checked()) {
			toggleAgeError(true);
			return false;
		}
		return true;
	}) | rpl::to_empty | rpl::start_to_stream(_agreeClicks, lifetime());

	if (_cancel) {
		addButton(std::move(_cancel), [] {})->clicks(
		) | rpl::to_empty | rpl::start_to_stream(_cancelClicks, lifetime());
	}

	if (age) {
		age->entity()->checkedChanges(
		) | rpl::on_next([=] {
			toggleAgeError(false);
		}, age->lifetime());

		heightValue(
		) | rpl::on_next([=](int height) {
			age->moveToLeft(0, height - age->height());
		}, age->lifetime());
	}

	content->resizeToWidth(st::boxWideWidth);

	using namespace rpl::mappers;
	rpl::combine(
		content->heightValue(),
		age ? age->heightValue() : rpl::single(0),
		_1 + _2
	) | rpl::on_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, content->lifetime());
}

void TermsBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		_agreeClicks.fire({});
	} else {
		BoxContent::keyPressEvent(e);
	}
}

QString TermsBox::lastClickedMention() const {
	return _lastClickedMention;
}

} // namespace Window
