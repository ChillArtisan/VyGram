#pragma once

#include "base/unique_qptr.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"

namespace Main {
class Account;
} // namespace Main

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Ayu {

void SwitchToAccount(
	not_null<Window::SessionController*> controller,
	not_null<Main::Account*> account,
	bool newWindow = false);
bool SwitchAccountRelative(
	not_null<Window::SessionController*> controller,
	int delta);
void AddAccount(not_null<Window::SessionController*> controller);

void ShowAccountsMenu(
	not_null<Window::SessionController*> controller,
	not_null<QWidget*> parent,
	QPoint globalPosition);
void ShowAccountAppearanceBox(
	not_null<Window::SessionController*> controller);
void ShowAccountAccentBox(
	not_null<Window::SessionController*> controller,
	not_null<Main::Account*> account);

class AccountsStrip final : public Ui::RpWidget {
public:
	AccountsStrip(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		bool compact);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Entry {
		not_null<Main::Account*> account;
		Ui::PeerUserpicView view;
	};

	void rebuild();
	void setOver(int index);
	void showContextMenu(int index, QPoint globalPosition);
	[[nodiscard]] int rowHeight() const;
	[[nodiscard]] int indexAt(QPoint position) const;

	const not_null<Window::SessionController*> _controller;
	const bool _compact = false;
	std::vector<Entry> _entries;
	rpl::lifetime _entriesLifetime;
	base::unique_qptr<Ui::PopupMenu> _menu;
	int _over = -1;
	int _pressed = -1;

};

} // namespace Ayu
