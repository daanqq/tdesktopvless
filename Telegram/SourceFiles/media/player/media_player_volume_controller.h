/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"

class QWheelEvent;

namespace Ui {
class MediaSlider;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Media::Player {

class Dropdown;

class VolumeController final : public Ui::RpWidget {
public:
	enum class Type {
		Song,
		Voice,
	};

	VolumeController(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void setIsVertical(bool vertical);
	void setType(Type type);
	void outerWheelEvent(not_null<QWheelEvent*> e);

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void setVolume(float64 volume);
	void applyVolumeChange(float64 volume);

	Type _type = Type::Song;
	object_ptr<Ui::MediaSlider> _slider;

};

[[nodiscard]] not_null<VolumeController*> PrepareVolumeDropdown(
	not_null<Dropdown*> dropdown,
	not_null<Window::SessionController*> controller,
	rpl::producer<not_null<QWheelEvent*>> outerWheelEvents);

} // namespace Media::Player
