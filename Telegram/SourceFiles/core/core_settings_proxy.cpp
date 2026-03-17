/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_settings_proxy.h"

#include "base/platform/base_platform_info.h"
#include "storage/serialize_common.h"

namespace Core {
namespace {

constexpr auto kProxyDataVersionTag = qint32(0x564c5353);
constexpr auto kProxyDataVersion = qint32(3);

[[nodiscard]] qint32 ProxySettingsToInt(MTP::ProxyData::Settings settings) {
	switch(settings) {
	case MTP::ProxyData::Settings::System: return 0;
	case MTP::ProxyData::Settings::Enabled: return 1;
	case MTP::ProxyData::Settings::Disabled: return 2;
	}
	Unexpected("Bad type in ProxySettingsToInt");
}

[[nodiscard]] MTP::ProxyData::Settings IntToProxySettings(qint32 value) {
	switch(value) {
	case 0: return MTP::ProxyData::Settings::System;
	case 1: return MTP::ProxyData::Settings::Enabled;
	case 2: return MTP::ProxyData::Settings::Disabled;
	}
	Unexpected("Bad type in IntToProxySettings");
}

[[nodiscard]] qint32 ProxyTypeToInt(MTP::ProxyData::Type type) {
	switch (type) {
	case MTP::ProxyData::Type::None: return 0;
	case MTP::ProxyData::Type::Socks5: return 1;
	case MTP::ProxyData::Type::Http: return 2;
	case MTP::ProxyData::Type::Mtproto: return 3;
	case MTP::ProxyData::Type::Vless: return 4;
	}
	Unexpected("Bad type in ProxyTypeToInt");
}

[[nodiscard]] MTP::ProxyData::Type IntToProxyType(qint32 value) {
	switch (value) {
	case 0: return MTP::ProxyData::Type::None;
	case 1: return MTP::ProxyData::Type::Socks5;
	case 2: return MTP::ProxyData::Type::Http;
	case 3: return MTP::ProxyData::Type::Mtproto;
	case 4: return MTP::ProxyData::Type::Vless;
	}
	Unexpected("Bad type in IntToProxyType");
}

[[nodiscard]] qint32 VlessTransportToInt(
		MTP::ProxyData::VlessTransport transport) {
	switch (transport) {
	case MTP::ProxyData::VlessTransport::Tcp: return 0;
	case MTP::ProxyData::VlessTransport::Ws: return 1;
	case MTP::ProxyData::VlessTransport::Xhttp: return 2;
	case MTP::ProxyData::VlessTransport::Grpc: return 3;
	}
	Unexpected("Bad type in VlessTransportToInt");
}

[[nodiscard]] MTP::ProxyData::VlessTransport IntToVlessTransport(
		qint32 value) {
	switch (value) {
	case 0: return MTP::ProxyData::VlessTransport::Tcp;
	case 1: return MTP::ProxyData::VlessTransport::Ws;
	case 2: return MTP::ProxyData::VlessTransport::Xhttp;
	case 3: return MTP::ProxyData::VlessTransport::Grpc;
	}
	Unexpected("Bad type in IntToVlessTransport");
}

[[nodiscard]] MTP::ProxyData::VlessTransport IntToVlessTransportV2(
		qint32 value) {
	switch (value) {
	case 0: return MTP::ProxyData::VlessTransport::Tcp;
	case 1: return MTP::ProxyData::VlessTransport::Ws;
	case 2: return MTP::ProxyData::VlessTransport::Grpc;
	}
	Unexpected("Bad type in IntToVlessTransportV2");
}

[[nodiscard]] qint32 VlessSecurityToInt(
		MTP::ProxyData::VlessSecurity security) {
	switch (security) {
	case MTP::ProxyData::VlessSecurity::None: return 0;
	case MTP::ProxyData::VlessSecurity::Tls: return 1;
	case MTP::ProxyData::VlessSecurity::Reality: return 2;
	}
	Unexpected("Bad type in VlessSecurityToInt");
}

[[nodiscard]] MTP::ProxyData::VlessSecurity IntToVlessSecurity(qint32 value) {
	switch (value) {
	case 0: return MTP::ProxyData::VlessSecurity::None;
	case 1: return MTP::ProxyData::VlessSecurity::Tls;
	case 2: return MTP::ProxyData::VlessSecurity::Reality;
	}
	Unexpected("Bad type in IntToVlessSecurity");
}

[[nodiscard]] MTP::ProxyData DeserializeProxyDataOld(
		QDataStream &stream,
		qint32 proxyType) {
	qint32 port = 0;
	MTP::ProxyData proxy;
	stream
		>> proxy.host
		>> port
		>> proxy.user
		>> proxy.password;
	proxy.port = port;
	proxy.type = IntToProxyType(proxyType);
	return proxy;
}

[[nodiscard]] MTP::ProxyData DeserializeProxyData(const QByteArray &data) {
	if (data.isEmpty()) {
		return MTP::ProxyData();
	}

	QDataStream stream(data);
	stream.setVersion(QDataStream::Qt_5_1);

	qint32 header = 0;
	stream >> header;
	if (!stream.status()) {
		return MTP::ProxyData();
	}
	if (header != kProxyDataVersionTag) {
		return DeserializeProxyDataOld(stream, header);
	}

	qint32 version = 0;
	qint32 proxyType = 0;
	qint32 port = 0;
	MTP::ProxyData proxy;
	stream
		>> version
		>> proxyType
		>> proxy.host
		>> port
		>> proxy.user
		>> proxy.password;
	if (!stream.status() || (version != 2 && version != kProxyDataVersion)) {
		return MTP::ProxyData();
	}
	proxy.port = port;
	proxy.type = IntToProxyType(proxyType);
	if (proxy.type == MTP::ProxyData::Type::Vless) {
		qint32 transport = 0;
		qint32 security = 0;
		qint32 allowInsecure = 0;
		if (version >= 3) {
			stream
				>> proxy.vless.id
				>> transport
				>> security
				>> proxy.vless.serverName
				>> proxy.vless.alpn
				>> allowInsecure
				>> proxy.vless.flow
				>> proxy.vless.fingerprint
				>> proxy.vless.publicKey
				>> proxy.vless.shortId
				>> proxy.vless.spiderX
				>> proxy.vless.path
				>> proxy.vless.hostHeader
				>> proxy.vless.mode
				>> proxy.vless.serviceName
				>> proxy.vless.authority;
		} else {
			stream
				>> proxy.vless.id
				>> transport
				>> security
				>> proxy.vless.serverName
				>> proxy.vless.alpn
				>> allowInsecure
				>> proxy.vless.fingerprint
				>> proxy.vless.publicKey
				>> proxy.vless.shortId
				>> proxy.vless.spiderX
				>> proxy.vless.path
				>> proxy.vless.hostHeader
				>> proxy.vless.serviceName
				>> proxy.vless.authority;
		}
		if (!stream.status()) {
			return MTP::ProxyData();
		}
		proxy.vless.transport = (version >= 3)
			? IntToVlessTransport(transport)
			: IntToVlessTransportV2(transport);
		proxy.vless.security = IntToVlessSecurity(security);
		proxy.vless.allowInsecure = (allowInsecure == 1);
	}
	return proxy;
}

[[nodiscard]] QByteArray SerializeProxyData(const MTP::ProxyData &proxy) {
	auto result = QByteArray();
	const auto size = 3 * sizeof(qint32)
		+ Serialize::stringSize(proxy.host)
		+ 1 * sizeof(qint32)
		+ Serialize::stringSize(proxy.user)
		+ Serialize::stringSize(proxy.password)
		+ ((proxy.type == MTP::ProxyData::Type::Vless)
			? Serialize::stringSize(proxy.vless.id)
				+ 3 * sizeof(qint32)
				+ Serialize::stringSize(proxy.vless.serverName)
				+ ranges::accumulate(
					proxy.vless.alpn,
					0,
					ranges::plus(),
					&Serialize::stringSize)
				+ Serialize::stringSize(proxy.vless.flow)
				+ Serialize::stringSize(proxy.vless.fingerprint)
				+ Serialize::stringSize(proxy.vless.publicKey)
				+ Serialize::stringSize(proxy.vless.shortId)
				+ Serialize::stringSize(proxy.vless.spiderX)
				+ Serialize::stringSize(proxy.vless.path)
				+ Serialize::stringSize(proxy.vless.hostHeader)
				+ Serialize::stringSize(proxy.vless.mode)
				+ Serialize::stringSize(proxy.vless.serviceName)
				+ Serialize::stringSize(proxy.vless.authority)
			: 0);

	result.reserve(size);
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		<< kProxyDataVersionTag
		<< kProxyDataVersion
		<< ProxyTypeToInt(proxy.type)
		<< proxy.host
		<< qint32(proxy.port)
		<< proxy.user
		<< proxy.password;
	if (proxy.type == MTP::ProxyData::Type::Vless) {
		stream
			<< proxy.vless.id
			<< VlessTransportToInt(proxy.vless.transport)
			<< VlessSecurityToInt(proxy.vless.security)
			<< proxy.vless.serverName
			<< proxy.vless.alpn
			<< qint32(proxy.vless.allowInsecure ? 1 : 0)
			<< proxy.vless.flow
			<< proxy.vless.fingerprint
			<< proxy.vless.publicKey
			<< proxy.vless.shortId
			<< proxy.vless.spiderX
			<< proxy.vless.path
			<< proxy.vless.hostHeader
			<< proxy.vless.mode
			<< proxy.vless.serviceName
			<< proxy.vless.authority;
	}
	return result;
}

} // namespace

SettingsProxy::SettingsProxy()
: _tryIPv6(!Platform::IsWindows()) {
}

QByteArray SettingsProxy::serialize() const {
	const auto serializedSelected = SerializeProxyData(_selected);
	const auto serializedList = ranges::views::all(
		_list
	) | ranges::views::transform(SerializeProxyData) | ranges::to_vector;

	const auto size = 3 * sizeof(qint32)
		+ Serialize::bytearraySize(serializedSelected)
		+ 1 * sizeof(qint32)
		+ ranges::accumulate(
			serializedList,
			0,
			ranges::plus(),
			&Serialize::bytearraySize);
	auto stream = Serialize::ByteArrayWriter(size);
	stream
		<< qint32(_tryIPv6 ? 1 : 0)
		<< qint32(_useProxyForCalls ? 1 : 0)
		<< ProxySettingsToInt(_settings)
		<< serializedSelected
		<< qint32(_list.size());
	for (const auto &i : serializedList) {
		stream << i;
	}
	return std::move(stream).result();
}

bool SettingsProxy::setFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return true;
	}

	auto stream = Serialize::ByteArrayReader(serialized);

	auto tryIPv6 = qint32(_tryIPv6 ? 1 : 0);
	auto useProxyForCalls = qint32(_useProxyForCalls ? 1 : 0);
	auto settings = ProxySettingsToInt(_settings);
	auto listCount = qint32(_list.size());
	auto selectedProxy = QByteArray();

	if (!stream.atEnd()) {
		stream
			>> tryIPv6
			>> useProxyForCalls
			>> settings
			>> selectedProxy
			>> listCount;
		if (stream.ok()) {
			for (auto i = 0; i != listCount; ++i) {
				QByteArray data;
				stream >> data;
				_list.push_back(DeserializeProxyData(data));
			}
		}
	}

	if (!stream.ok()) {
		LOG(("App Error: "
			"Bad data for Core::SettingsProxy::setFromSerialized()"));
		return false;
	}

	_tryIPv6 = (tryIPv6 == 1);
	_useProxyForCalls = (useProxyForCalls == 1);
	_settings = IntToProxySettings(settings);
	_selected = DeserializeProxyData(selectedProxy);

	return true;
}

bool SettingsProxy::isEnabled() const {
	return _settings == MTP::ProxyData::Settings::Enabled;
}

bool SettingsProxy::isSystem() const {
	return _settings == MTP::ProxyData::Settings::System;
}

bool SettingsProxy::isDisabled() const {
	return _settings == MTP::ProxyData::Settings::Disabled;
}

bool SettingsProxy::tryIPv6() const {
	return _tryIPv6;
}

void SettingsProxy::setTryIPv6(bool value) {
	_tryIPv6 = value;
}

bool SettingsProxy::useProxyForCalls() const {
	return _useProxyForCalls;
}

void SettingsProxy::setUseProxyForCalls(bool value) {
	_useProxyForCalls = value;
}

MTP::ProxyData::Settings SettingsProxy::settings() const {
	return _settings;
}

void SettingsProxy::setSettings(MTP::ProxyData::Settings value) {
	_settings = value;
}

MTP::ProxyData SettingsProxy::selected() const {
	return _selected;
}

void SettingsProxy::setSelected(MTP::ProxyData value) {
	_selected = value;
}

const std::vector<MTP::ProxyData> &SettingsProxy::list() const {
	return _list;
}

std::vector<MTP::ProxyData> &SettingsProxy::list() {
	return _list;
}

rpl::producer<> SettingsProxy::connectionTypeValue() const {
	return _connectionTypeChanges.events_starting_with({});
}

rpl::producer<> SettingsProxy::connectionTypeChanges() const {
	return _connectionTypeChanges.events();
}

void SettingsProxy::connectionTypeChangesNotify() {
	_connectionTypeChanges.fire({});
}

} // namespace Core
