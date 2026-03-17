/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/xray_proxy_manager.h"

#include "settings.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QTcpServer>

namespace Core {
namespace {

constexpr auto kStartTimeout = 3000;
constexpr auto kLocalProxyTimeout = 3000;

[[nodiscard]] QString HashProxy(const MTP::ProxyData &proxy) {
	auto basis = proxy.toVlessLink();
	if (basis.isEmpty()) {
		basis = proxy.host + u':' + QString::number(proxy.port);
	}
	return QString::fromLatin1(QCryptographicHash::hash(
		basis.toUtf8(),
		QCryptographicHash::Md5).toHex());
}

[[nodiscard]] QString XrayNetwork(MTP::ProxyData::VlessTransport transport) {
	switch (transport) {
	case MTP::ProxyData::VlessTransport::Tcp: return u"raw"_q;
	case MTP::ProxyData::VlessTransport::Ws: return u"ws"_q;
	case MTP::ProxyData::VlessTransport::Xhttp: return u"xhttp"_q;
	case MTP::ProxyData::VlessTransport::Grpc: return u"grpc"_q;
	}
	Unexpected("VLESS transport in XrayNetwork.");
}

[[nodiscard]] QString XraySecurity(MTP::ProxyData::VlessSecurity security) {
	switch (security) {
	case MTP::ProxyData::VlessSecurity::None: return u"none"_q;
	case MTP::ProxyData::VlessSecurity::Tls: return u"tls"_q;
	case MTP::ProxyData::VlessSecurity::Reality: return u"reality"_q;
	}
	Unexpected("VLESS security in XraySecurity.");
}

[[nodiscard]] bool WaitForLocalProxy(int port) {
	auto socket = QTcpSocket();
	socket.connectToHost(QHostAddress::LocalHost, port);
	return socket.waitForConnected(kLocalProxyTimeout);
}

[[nodiscard]] QString LastNonEmptyLogLine(const QString &path) {
	auto file = QFile(path);
	if (!file.exists()) {
		return QString();
	}
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return QString();
	}
	const auto lines = QString::fromUtf8(file.readAll()).split('\n');
	for (auto i = lines.crbegin(); i != lines.crend(); ++i) {
		const auto line = i->trimmed();
		if (!line.isEmpty()) {
			return line;
		}
	}
	return QString();
}

} // namespace

XrayProxyManager::XrayProxyManager(QObject *parent)
: QObject(parent) {
}

XrayProxyManager::~XrayProxyManager() {
	stop();
}

void XrayProxyManager::setStateChangedCallback(std::function<void()> callback) {
	_stateChangedCallback = std::move(callback);
}

void XrayProxyManager::applyProxy(
		const MTP::ProxyData &proxy,
		MTP::ProxyData::Settings settings) {
	if (settings != MTP::ProxyData::Settings::Enabled
		|| proxy.type != MTP::ProxyData::Type::Vless) {
		stop();
		return;
	}
	start(proxy);
}

void XrayProxyManager::stop() {
	if (_process) {
		_process->disconnect(this);
		if (_process->state() != QProcess::NotRunning) {
			_process->kill();
			_process->waitForFinished(1000);
		}
		_process.reset();
	}
	clearState();
	_state = State::Stopped;
}

auto XrayProxyManager::state() const -> State {
	return _state;
}

QString XrayProxyManager::lastError() const {
	return _lastError;
}

MTP::ProxyData XrayProxyManager::effectiveProxy() const {
	return (_state == State::Running || _state == State::Failed)
		? _effectiveProxy
		: MTP::ProxyData();
}

QString XrayProxyManager::executablePath() const {
#ifdef Q_OS_WIN
	return QDir::cleanPath(cExeDir() + u"xray/xray.exe"_q);
#elif defined Q_OS_MAC
	return QDir::cleanPath(cExeDir() + u"../Resources/xray/xray"_q);
#else
	return QDir::cleanPath(cExeDir() + u"xray/xray"_q);
#endif
}

QString XrayProxyManager::runtimeDirectory(const MTP::ProxyData &proxy) const {
	return cWorkingDir() + u"tdata/xray/"_q + HashProxy(proxy) + u'/';
}

QString XrayProxyManager::configPath(const MTP::ProxyData &proxy) const {
	return runtimeDirectory(proxy) + u"config.json"_q;
}

QString XrayProxyManager::logPath(const MTP::ProxyData &proxy) const {
	return runtimeDirectory(proxy) + u"xray.log"_q;
}

int XrayProxyManager::takeLocalPort() const {
	auto server = QTcpServer();
	if (!server.listen(QHostAddress::LocalHost, 0)) {
		return 0;
	}
	return server.serverPort();
}

QByteArray XrayProxyManager::runtimeConfig(
		const MTP::ProxyData &proxy,
		int localPort) const {
	if (!proxy || proxy.type != MTP::ProxyData::Type::Vless) {
		return QByteArray();
	}

	const auto &config = proxy.vless;
	auto user = QJsonObject{
		{ u"id"_q, config.id },
		{ u"encryption"_q, u"none"_q },
	};
	if (!config.flow.isEmpty()) {
		user.insert(u"flow"_q, config.flow);
	}
	auto streamSettings = QJsonObject{
		{ u"network"_q, XrayNetwork(config.transport) },
		{ u"security"_q, XraySecurity(config.security) },
	};
	if (config.transport == MTP::ProxyData::VlessTransport::Ws) {
		auto wsSettings = QJsonObject();
		if (!config.path.isEmpty()) {
			wsSettings.insert(u"path"_q, config.path);
		}
		if (!config.hostHeader.isEmpty()) {
			wsSettings.insert(u"headers"_q, QJsonObject{
				{ u"Host"_q, config.hostHeader },
			});
		}
		streamSettings.insert(u"wsSettings"_q, wsSettings);
	} else if (config.transport == MTP::ProxyData::VlessTransport::Xhttp) {
		auto xhttpSettings = QJsonObject();
		if (!config.path.isEmpty()) {
			xhttpSettings.insert(u"path"_q, config.path);
		}
		if (!config.hostHeader.isEmpty()) {
			xhttpSettings.insert(u"host"_q, config.hostHeader);
		}
		if (!config.mode.isEmpty()) {
			xhttpSettings.insert(u"mode"_q, config.mode);
		}
		streamSettings.insert(u"xhttpSettings"_q, xhttpSettings);
	} else if (config.transport == MTP::ProxyData::VlessTransport::Grpc) {
		auto grpcSettings = QJsonObject{
			{ u"serviceName"_q, config.serviceName },
			{ u"multiMode"_q, false },
		};
		if (!config.authority.isEmpty()) {
			grpcSettings.insert(u"authority"_q, config.authority);
		}
		streamSettings.insert(u"grpcSettings"_q, grpcSettings);
	}
	if (config.security == MTP::ProxyData::VlessSecurity::Tls) {
		auto tlsSettings = QJsonObject();
		if (!config.serverName.isEmpty()) {
			tlsSettings.insert(u"serverName"_q, config.serverName);
		}
		if (!config.alpn.isEmpty()) {
			auto alpn = QJsonArray();
			for (const auto &value : config.alpn) {
				alpn.push_back(value);
			}
			tlsSettings.insert(u"alpn"_q, alpn);
		}
		if (!config.fingerprint.isEmpty()) {
			tlsSettings.insert(u"fingerprint"_q, config.fingerprint);
		}
		if (config.allowInsecure) {
			tlsSettings.insert(u"allowInsecure"_q, true);
		}
		streamSettings.insert(u"tlsSettings"_q, tlsSettings);
	} else if (config.security == MTP::ProxyData::VlessSecurity::Reality) {
		streamSettings.insert(u"realitySettings"_q, QJsonObject{
			{ u"serverName"_q, config.serverName },
			{ u"fingerprint"_q, config.fingerprint },
			{ u"password"_q, config.publicKey },
			{ u"shortId"_q, config.shortId },
			{ u"spiderX"_q, config.spiderX },
		});
	}

	const auto root = QJsonObject{
		{ u"log"_q, QJsonObject{
			{ u"loglevel"_q, u"warning"_q },
		} },
		{ u"inbounds"_q, QJsonArray{
			QJsonObject{
				{ u"listen"_q, u"127.0.0.1"_q },
				{ u"port"_q, localPort },
				{ u"protocol"_q, u"socks"_q },
				{ u"settings"_q, QJsonObject{
					{ u"auth"_q, u"noauth"_q },
					{ u"udp"_q, false },
				} },
			},
		} },
		{ u"outbounds"_q, QJsonArray{
			QJsonObject{
				{ u"protocol"_q, u"vless"_q },
				{ u"settings"_q, QJsonObject{
					{ u"vnext"_q, QJsonArray{
						QJsonObject{
							{ u"address"_q, proxy.host },
							{ u"port"_q, int(proxy.port) },
							{ u"users"_q, QJsonArray{ user } },
						},
					} },
				} },
				{ u"streamSettings"_q, streamSettings },
			},
		} },
	};
	return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

MTP::ProxyData XrayProxyManager::localProxy(int localPort) const {
	return MTP::ProxyData{
		.type = MTP::ProxyData::Type::Socks5,
		.host = u"127.0.0.1"_q,
		.port = uint32(localPort),
	};
}

void XrayProxyManager::start(const MTP::ProxyData &proxy) {
	stop();

	_state = State::Starting;
	_sourceProxy = proxy;

	const auto executable = executablePath();
	if (!QFileInfo::exists(executable)) {
		fail(u"Xray executable was not found."_q);
		return;
	}

	const auto localPort = takeLocalPort();
	if (!localPort) {
		fail(u"Could not allocate local SOCKS5 port."_q);
		return;
	}

	const auto directory = runtimeDirectory(proxy);
	if (!QDir().mkpath(directory)) {
		fail(u"Could not create Xray runtime directory."_q);
		return;
	}

	{
		auto file = QFile(configPath(proxy));
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			fail(u"Could not write Xray config."_q);
			return;
		}
		file.write(runtimeConfig(proxy, localPort));
		file.close();
	}

	_process = std::make_unique<QProcess>();
	_process->setProgram(executable);
	_process->setArguments({
		u"-config"_q,
		configPath(proxy),
	});
	_process->setWorkingDirectory(QFileInfo(executable).absolutePath());
	_process->setStandardOutputFile(logPath(proxy), QIODevice::Append);
	_process->setStandardErrorFile(logPath(proxy), QIODevice::Append);
	connect(
		_process.get(),
		&QProcess::errorOccurred,
		this,
		[=](QProcess::ProcessError) {
			if (_state != State::Stopped) {
				fail(_process ? _process->errorString() : QString());
			}
		});
	connect(
		_process.get(),
		qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
		this,
		[=](int, QProcess::ExitStatus) {
			if (_state == State::Running) {
				fail(u"Xray process stopped unexpectedly."_q);
			} else if (_state == State::Starting) {
				clearState();
				_state = State::Stopped;
			}
		});
	_process->start();
	if (!_process->waitForStarted(kStartTimeout)) {
		fail(_process->errorString());
		return;
	}
	if (!WaitForLocalProxy(localPort)) {
		const auto logError = LastNonEmptyLogLine(logPath(proxy));
		fail(logError.isEmpty()
			? u"Xray local SOCKS5 port did not become ready."_q
			: logError);
		return;
	}

	_effectiveProxy = localProxy(localPort);
	_lastError = QString();
	_state = State::Running;
}

void XrayProxyManager::fail(const QString &error) {
	const auto keepEffectiveProxy = (_state == State::Running);
	if (_process) {
		_process->disconnect(this);
		if (_process->state() != QProcess::NotRunning) {
			_process->kill();
			_process->waitForFinished(1000);
		}
		_process.reset();
	}
	if (!keepEffectiveProxy) {
		clearState();
	}
	_lastError = error;
	_state = State::Failed;
	if (keepEffectiveProxy && _stateChangedCallback) {
		_stateChangedCallback();
	}
}

void XrayProxyManager::clearState() {
	_sourceProxy = MTP::ProxyData();
	_effectiveProxy = MTP::ProxyData();
	if (_state != State::Failed) {
		_lastError = QString();
	}
}

} // namespace Core
