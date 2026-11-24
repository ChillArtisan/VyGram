#pragma once

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Ayu {

void ShowChatPrivacy(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

} // namespace Ayu
