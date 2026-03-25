/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_proxy_data.h"

#include <functional>
#include <memory>

#include <QtCore/QObject>

class QProcess;

namespace Core {

class XrayProxyManager final : public QObject {
public:
	enum class State {
		Stopped,
		Starting,
		Running,
		Failed,
	};

	explicit XrayProxyManager(QObject *parent = nullptr);
	~XrayProxyManager();

	void applyProxy(
		const MTP::ProxyData &proxy,
		MTP::ProxyData::Settings settings);
	void setStateChangedCallback(std::function<void()> callback);
	void stop();

	[[nodiscard]] State state() const;
	[[nodiscard]] QString lastError() const;
	[[nodiscard]] MTP::ProxyData effectiveProxy() const;

private:
	[[nodiscard]] QString executablePath() const;
	[[nodiscard]] QString runtimeDirectory(const MTP::ProxyData &proxy) const;
	[[nodiscard]] QString configPath(const MTP::ProxyData &proxy) const;
	[[nodiscard]] QString logPath(const MTP::ProxyData &proxy) const;
	[[nodiscard]] int takeLocalPort() const;
	[[nodiscard]] QByteArray runtimeConfig(
		const MTP::ProxyData &proxy,
		int localPort) const;
	[[nodiscard]] MTP::ProxyData localProxy(int localPort) const;
	[[nodiscard]] bool sameProxyRunning(const MTP::ProxyData &proxy) const;

	void start(const MTP::ProxyData &proxy);
	void cleanupProcess();
	void closeJob();
	void fail(const QString &error);
	void clearState();

	std::function<void()> _stateChangedCallback;
	State _state = State::Stopped;
	QString _lastError;
	MTP::ProxyData _sourceProxy;
	MTP::ProxyData _effectiveProxy;
	std::unique_ptr<QProcess> _process;
#ifdef Q_OS_WIN
	void *_job = nullptr;
#endif

};

} // namespace Core
