#pragma once

#include "api/api_common.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Ayu {

void ShowSendTemplates(
	not_null<Window::SessionController*> controller,
	Ui::RpWidget *context = nullptr,
	Fn<Api::SendAction()> actionFactory = nullptr);

} // namespace Ayu
