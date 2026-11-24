#pragma once

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Ayu {

void ShowChatLabelEditor(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
void ShowChatLabelFilter(not_null<Window::SessionController*> controller);

} // namespace Ayu
