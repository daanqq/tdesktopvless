/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_volume_controller.h"

#include "media/audio/media_audio.h"
#include "media/player/media_player_dropdown.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "styles/style_media_player.h"
#include "styles/style_widgets.h"

#include <QtGui/QGuiApplication>

namespace Media::Player {
namespace {

[[nodiscard]] float64 CurrentVolume(VolumeController::Type type) {
	switch (type) {
	case VolumeController::Type::Song:
		return Core::App().settings().songVolume();
	case VolumeController::Type::Voice:
		return Core::App().settings().voiceVolume();
	}
	Unexpected("Type in CurrentVolume.");
	return 0.;
}

[[nodiscard]] float64 SliderRatio(
		VolumeController::Type type,
		float64 volume) {
	switch (type) {
	case VolumeController::Type::Song:
		return volume;
	case VolumeController::Type::Voice:
		return volume / Core::Settings::kMaxVoiceVolume;
	}
	Unexpected("Type in SliderRatio.");
	return 0.;
}

[[nodiscard]] float64 VolumeFromSliderRatio(
		VolumeController::Type type,
		float64 ratio) {
	switch (type) {
	case VolumeController::Type::Song:
		return ratio;
	case VolumeController::Type::Voice:
		return ratio * Core::Settings::kMaxVoiceVolume;
	}
	Unexpected("Type in VolumeFromSliderRatio.");
	return 0.;
}

void RememberVolume(VolumeController::Type type, float64 volume) {
	if (volume <= 0) {
		return;
	}
	switch (type) {
	case VolumeController::Type::Song:
		Core::App().settings().setRememberedSongVolume(volume);
		break;
	case VolumeController::Type::Voice:
		Core::App().settings().setRememberedVoiceVolume(volume);
		break;
	}
}

void SaveVolume(VolumeController::Type type, float64 volume) {
	switch (type) {
	case VolumeController::Type::Song:
		if (volume != Core::App().settings().songVolume()) {
			mixer()->setSongVolume(volume);
			Core::App().settings().setSongVolume(volume);
		}
		break;
	case VolumeController::Type::Voice:
		if (volume != Core::App().settings().voiceVolume()) {
			mixer()->setVoiceVolume(volume);
			Core::App().settings().setVoiceVolume(volume);
		}
		break;
	}
}

} // namespace

VolumeController::VolumeController(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _slider(this, st::mediaPlayerPanelPlayback) {
	_slider->setMoveByWheel(true);
	_slider->setChangeProgressCallback([=](float64 ratio) {
		applyVolumeChange(VolumeFromSliderRatio(_type, ratio));
	});
	_slider->setChangeFinishedCallback([=](float64 ratio) {
		const auto volume = VolumeFromSliderRatio(_type, ratio);
		if (volume > 0) {
			RememberVolume(_type, volume);
		}
		applyVolumeChange(volume);
		Core::App().saveSettingsDelayed();
	});
	Core::App().settings().songVolumeChanges(
	) | rpl::on_next([=](float64 volume) {
		if ((_type == Type::Song) && !_slider->isChanging()) {
			_slider->setValue(SliderRatio(_type, volume));
		}
	}, lifetime());
	Core::App().settings().voiceVolumeChanges(
	) | rpl::on_next([=](float64 volume) {
		if ((_type == Type::Voice) && !_slider->isChanging()) {
			_slider->setValue(SliderRatio(_type, volume));
		}
	}, lifetime());
	setType(Type::Song);

	resize(st::mediaPlayerPanelVolumeWidth, 2 * st::mediaPlayerPanelPlaybackPadding + st::mediaPlayerPanelPlayback.width);
}

void VolumeController::setIsVertical(bool vertical) {
	using Direction = Ui::MediaSlider::Direction;
	_slider->setDirection(vertical ? Direction::Vertical : Direction::Horizontal);
	_slider->setAlwaysDisplayMarker(vertical);
}

void VolumeController::setType(Type type) {
	_type = type;
	_slider->clearDividers();
	if (_type == Type::Voice) {
		_slider->addDivider(
			1. / Core::Settings::kMaxVoiceVolume,
			st::mediaPlayerVolumeDivider);
	}
	setVolume(CurrentVolume(_type));
}

void VolumeController::outerWheelEvent(not_null<QWheelEvent*> e) {
	QGuiApplication::sendEvent(_slider.data(), e);
}

void VolumeController::resizeEvent(QResizeEvent *e) {
	_slider->setGeometry(rect());
}

void VolumeController::setVolume(float64 volume) {
	_slider->setValue(SliderRatio(_type, volume));
	if (volume > 0) {
		RememberVolume(_type, volume);
	}
	applyVolumeChange(volume);
}

void VolumeController::applyVolumeChange(float64 volume) {
	SaveVolume(_type, volume);
}

not_null<VolumeController*> PrepareVolumeDropdown(
		not_null<Dropdown*> dropdown,
		not_null<Window::SessionController*> controller,
		rpl::producer<not_null<QWheelEvent*>> outerWheelEvents) {
	const auto volume = Ui::CreateChild<VolumeController>(
		dropdown.get(),
		controller);
	volume->show();
	volume->setIsVertical(true);

	dropdown->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto rect = QRect(QPoint(), size);
		const auto inner = rect.marginsRemoved(dropdown->getMargin());
		volume->setGeometry(
			inner.x(),
			inner.y() - st::lineWidth,
			inner.width(),
			(inner.height()
				+ st::lineWidth
				- ((st::mediaPlayerVolumeSize.width()
					- st::mediaPlayerPanelPlayback.width) / 2)));
	}, volume->lifetime());

	std::move(
		outerWheelEvents
	) | rpl::on_next([=](not_null<QWheelEvent*> e) {
		volume->outerWheelEvent(e);
	}, volume->lifetime());

	return volume;
}

} // namespace Media::Player
