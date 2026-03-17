/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtproto_proxy_data.h"

#include "base/options.h"
#include "base/qthelp_url.h"
#include "base/qt/qt_string_view.h"

#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QUuid>

#include <algorithm>
#include <array>
#include <limits>

namespace MTP {
namespace {

base::options::toggle OptionVlessProxyForCalls({
	.id = "vless-proxy-for-calls",
	.name = "VLESS proxy for calls",
	.description = "Allow using effective local SOCKS5 from Xray for calls.",
});

[[nodiscard]] QString Trimmed(const QString &value) {
	return value.trimmed();
}

[[nodiscard]] QString LowerTrimmed(const QString &value) {
	return Trimmed(value).toLower();
}

[[nodiscard]] QString NormalizeUuid(const QString &value) {
	const auto uuid = QUuid(Trimmed(value));
	return uuid.isNull()
		? QString()
		: uuid.toString(QUuid::WithoutBraces).toLower();
}

[[nodiscard]] QString NormalizeOptionalValue(const QString &value) {
	return Trimmed(value);
}

[[nodiscard]] QString NormalizeWsPath(const QString &value) {
	auto result = Trimmed(value);
	if (!result.isEmpty() && !result.startsWith('/')) {
		result.prepend('/');
	}
	return result;
}

[[nodiscard]] QStringList NormalizeAlpn(const QStringList &values) {
	auto result = QStringList();
	for (const auto &value : values) {
		const auto normalized = LowerTrimmed(value);
		if (!normalized.isEmpty() && !result.contains(normalized)) {
			result.push_back(normalized);
		}
	}
	return result;
}

[[nodiscard]] QStringList ParseAlpn(const QString &value) {
	return NormalizeAlpn(value.split(',', Qt::SkipEmptyParts));
}

[[nodiscard]] bool IsBase64UrlValue(const QString &value) {
	if (value.isEmpty()) {
		return false;
	}
	const auto bad = [](QChar ch) {
		const auto code = ch.unicode();
		return (code < 'a' || code > 'z')
			&& (code < 'A' || code > 'Z')
			&& (code < '0' || code > '9')
			&& (code != '_')
			&& (code != '-');
	};
	return (std::find_if(value.begin(), value.end(), bad) == value.end());
}

[[nodiscard]] bool IsHexValue(const QString &value) {
	const auto bad = [](QChar ch) {
		const auto code = ch.unicode();
		return (code < 'a' || code > 'f')
			&& (code < 'A' || code > 'F')
			&& (code < '0' || code > '9');
	};
	return (std::find_if(value.begin(), value.end(), bad) == value.end());
}

[[nodiscard]] bool IsAlpnValue(const QString &value) {
	if (value.isEmpty()) {
		return false;
	}
	const auto bad = [](QChar ch) {
		const auto code = ch.unicode();
		return (code < 'a' || code > 'z')
			&& (code < '0' || code > '9')
			&& (code != '-')
			&& (code != '.')
			&& (code != '/');
	};
	return (std::find_if(value.begin(), value.end(), bad) == value.end());
}

[[nodiscard]] bool IsIpv4Host(const QString &value) {
	static const auto kIpv4 = QRegularExpression(
		QStringLiteral("^\\d+\\.\\d+\\.\\d+\\.\\d+$"));
	return kIpv4.match(value).hasMatch();
}

[[nodiscard]] bool SupportsImplicitServerName(const QString &host) {
	return !host.isEmpty()
		&& !IsIpv4Host(host)
		&& !qthelp::is_ipv6(host);
}

[[nodiscard]] QString QueryValue(
		const QUrlQuery &query,
		std::initializer_list<QString> names) {
	for (const auto &name : names) {
		if (query.hasQueryItem(name)) {
			return query.queryItemValue(name);
		}
	}
	return QString();
}

[[nodiscard]] QString DecodedQueryValue(
		const QUrlQuery &query,
		std::initializer_list<QString> names) {
	const auto value = QueryValue(query, names);
	return value.isEmpty()
		? QString()
		: QUrl::fromPercentEncoding(value.toUtf8());
}

[[nodiscard]] bool HasNonEmptyQueryValue(
		const QUrlQuery &query,
		std::initializer_list<QString> names) {
	return !Trimmed(QueryValue(query, names)).isEmpty();
}

[[nodiscard]] bool ParseBool(const QString &value) {
	const auto normalized = LowerTrimmed(value);
	return (normalized == u"1"_q)
		|| (normalized == u"true"_q)
		|| (normalized == u"yes"_q)
		|| (normalized == u"on"_q);
}

[[nodiscard]] QString JoinList(const QStringList &values) {
	return values.join(u',');
}

[[nodiscard]] std::optional<ProxyData::VlessTransport> ParseVlessTransport(
		const QString &value) {
	const auto normalized = LowerTrimmed(value);
	if (normalized.isEmpty()
		|| normalized == u"tcp"_q
		|| normalized == u"raw"_q) {
		return ProxyData::VlessTransport::Tcp;
	} else if (normalized == u"ws"_q
		|| normalized == u"websocket"_q) {
		return ProxyData::VlessTransport::Ws;
	} else if (normalized == u"xhttp"_q) {
		return ProxyData::VlessTransport::Xhttp;
	} else if (normalized == u"grpc"_q) {
		return ProxyData::VlessTransport::Grpc;
	}
	return std::nullopt;
}

[[nodiscard]] QString SerializeVlessTransport(
		ProxyData::VlessTransport value) {
	switch (value) {
	case ProxyData::VlessTransport::Tcp: return u"tcp"_q;
	case ProxyData::VlessTransport::Ws: return u"ws"_q;
	case ProxyData::VlessTransport::Xhttp: return u"xhttp"_q;
	case ProxyData::VlessTransport::Grpc: return u"grpc"_q;
	}
	Unexpected("VLESS transport in SerializeVlessTransport.");
}

[[nodiscard]] QString NormalizeFlow(const QString &value) {
	return LowerTrimmed(value);
}

[[nodiscard]] QString NormalizeMode(const QString &value) {
	return LowerTrimmed(value);
}

[[nodiscard]] bool IsSupportedFlow(const QString &value) {
	return value.isEmpty() || value == u"xtls-rprx-vision"_q;
}

[[nodiscard]] bool IsSupportedXhttpMode(const QString &value) {
	return value.isEmpty()
		|| value == u"auto"_q
		|| value == u"packet-up"_q
		|| value == u"stream-up"_q
		|| value == u"stream-one"_q;
}

[[nodiscard]] std::optional<ProxyData::VlessSecurity> ParseVlessSecurity(
		const QString &value) {
	const auto normalized = LowerTrimmed(value);
	if (normalized.isEmpty() || normalized == u"none"_q) {
		return ProxyData::VlessSecurity::None;
	} else if (normalized == u"tls"_q) {
		return ProxyData::VlessSecurity::Tls;
	} else if (normalized == u"reality"_q) {
		return ProxyData::VlessSecurity::Reality;
	}
	return std::nullopt;
}

[[nodiscard]] QString SerializeVlessSecurity(
		ProxyData::VlessSecurity value) {
	switch (value) {
	case ProxyData::VlessSecurity::None: return u"none"_q;
	case ProxyData::VlessSecurity::Tls: return u"tls"_q;
	case ProxyData::VlessSecurity::Reality: return u"reality"_q;
	}
	Unexpected("VLESS security in SerializeVlessSecurity.");
}

[[nodiscard]] ProxyData::VlessConfig NormalizeVlessConfig(
		ProxyData::VlessConfig value) {
	value.id = NormalizeUuid(value.id);
	value.serverName = NormalizeOptionalValue(value.serverName);
	value.alpn = NormalizeAlpn(value.alpn);
	value.flow = NormalizeFlow(value.flow);
	value.fingerprint = LowerTrimmed(value.fingerprint);
	value.publicKey = NormalizeOptionalValue(value.publicKey);
	value.shortId = LowerTrimmed(value.shortId);
	value.spiderX = NormalizeOptionalValue(value.spiderX);
	value.path = NormalizeWsPath(value.path);
	value.hostHeader = NormalizeOptionalValue(value.hostHeader);
	value.mode = NormalizeMode(value.mode);
	value.serviceName = NormalizeOptionalValue(value.serviceName);
	value.authority = NormalizeOptionalValue(value.authority);
	return value;
}

[[nodiscard]] ProxyData::Status VlessStatus(
		const QString &host,
		const ProxyData::VlessConfig &config) {
	if (config.id.isEmpty()) {
		return ProxyData::Status::Invalid;
	}
	if (!std::all_of(config.alpn.begin(), config.alpn.end(), &IsAlpnValue)) {
		return ProxyData::Status::Invalid;
	}
	if (!IsSupportedFlow(config.flow) || !IsSupportedXhttpMode(config.mode)) {
		return ProxyData::Status::Unsupported;
	}
	if (!config.publicKey.isEmpty() && !IsBase64UrlValue(config.publicKey)) {
		return ProxyData::Status::Invalid;
	}
	if (!config.shortId.isEmpty()) {
		if (!IsHexValue(config.shortId)
			|| config.shortId.size() > 16
			|| (config.shortId.size() % 2 != 0)) {
			return ProxyData::Status::Invalid;
		}
	}
	switch (config.transport) {
	case ProxyData::VlessTransport::Tcp:
		if (!config.path.isEmpty()
			|| !config.hostHeader.isEmpty()
			|| !config.mode.isEmpty()
			|| !config.serviceName.isEmpty()
			|| !config.authority.isEmpty()) {
			return ProxyData::Status::Invalid;
		}
		break;
	case ProxyData::VlessTransport::Ws:
		if (config.security == ProxyData::VlessSecurity::Reality) {
			return ProxyData::Status::Unsupported;
		} else if (!config.flow.isEmpty()
			|| !config.mode.isEmpty()
			|| !config.serviceName.isEmpty()
			|| !config.authority.isEmpty()) {
			return ProxyData::Status::Invalid;
		}
		break;
	case ProxyData::VlessTransport::Xhttp:
		if (!config.serviceName.isEmpty()
			|| !config.authority.isEmpty()) {
			return ProxyData::Status::Invalid;
		}
		break;
	case ProxyData::VlessTransport::Grpc:
		if (config.security != ProxyData::VlessSecurity::Tls) {
			return ProxyData::Status::Unsupported;
		} else if (config.serviceName.isEmpty()) {
			return ProxyData::Status::Invalid;
		} else if (!config.flow.isEmpty()
			|| !config.path.isEmpty()
			|| !config.hostHeader.isEmpty()
			|| !config.mode.isEmpty()) {
			return ProxyData::Status::Invalid;
		} else if (!config.alpn.isEmpty() && config.alpn.front() != u"h2"_q) {
			return ProxyData::Status::Invalid;
		}
		break;
	}
	if (config.security == ProxyData::VlessSecurity::Reality) {
		if (config.transport != ProxyData::VlessTransport::Tcp
			&& config.transport != ProxyData::VlessTransport::Xhttp) {
			return ProxyData::Status::Unsupported;
		} else if (config.serverName.isEmpty()
			|| config.fingerprint.isEmpty()
			|| config.publicKey.isEmpty()) {
			return ProxyData::Status::Invalid;
		} else if (config.allowInsecure
			|| !config.alpn.isEmpty()
			|| config.fingerprint == u"unsafe"_q) {
			return ProxyData::Status::Invalid;
		}
	} else if (config.security == ProxyData::VlessSecurity::Tls) {
		if (config.serverName.isEmpty() && !SupportsImplicitServerName(host)) {
			return ProxyData::Status::Invalid;
		} else if (!config.publicKey.isEmpty()
			|| !config.shortId.isEmpty()
			|| !config.spiderX.isEmpty()) {
			return ProxyData::Status::Invalid;
		}
	} else if (!config.serverName.isEmpty()
		|| !config.alpn.isEmpty()
		|| config.allowInsecure
		|| !config.fingerprint.isEmpty()
		|| !config.publicKey.isEmpty()
		|| !config.shortId.isEmpty()
		|| !config.spiderX.isEmpty()) {
		return ProxyData::Status::Invalid;
	}
	if (!config.flow.isEmpty()
		&& (config.transport != ProxyData::VlessTransport::Tcp
			|| (config.security != ProxyData::VlessSecurity::Tls
				&& config.security != ProxyData::VlessSecurity::Reality))) {
		return ProxyData::Status::Unsupported;
	}
	return ProxyData::Status::Valid;
}

struct ParsedVlessLink {
	std::optional<ProxyData> proxy;
	ProxyData::Status status = ProxyData::Status::Invalid;
};

[[nodiscard]] bool IsSupportedVlessQueryItem(const QString &name) {
	const auto normalized = LowerTrimmed(name);
	static const auto kSupported = std::array<QStringView, 23>{
		u"encryption"_q,
		u"type"_q,
		u"transport"_q,
		u"security"_q,
		u"sni"_q,
		u"servername"_q,
		u"alpn"_q,
		u"allowinsecure"_q,
		u"flow"_q,
		u"fp"_q,
		u"fingerprint"_q,
		u"pbk"_q,
		u"publickey"_q,
		u"password"_q,
		u"sid"_q,
		u"shortid"_q,
		u"spx"_q,
		u"spiderx"_q,
		u"path"_q,
		u"host"_q,
		u"mode"_q,
		u"servicename"_q,
		u"authority"_q,
	};
	return (std::find(
		kSupported.begin(),
		kSupported.end(),
		QStringView(normalized)) != kSupported.end());
}

[[nodiscard]] ParsedVlessLink ParseVlessLink(const QString &link) {
	const auto url = QUrl(link);
	if (!url.isValid()
		|| url.scheme().compare(u"vless"_q, Qt::CaseInsensitive) != 0) {
		return {};
	}
	if (!url.password().isEmpty()) {
		return {};
	}
	const auto rawPath = Trimmed(url.path());
	if (!rawPath.isEmpty() && rawPath != u"/"_q) {
		return {};
	}

	const auto port = url.port();
	if (port <= 0 || port > std::numeric_limits<uint16>::max()) {
		return {};
	}

	const auto query = QUrlQuery(url);
	for (const auto &[name, value] : query.queryItems()) {
		if (!Trimmed(value).isEmpty() && !IsSupportedVlessQueryItem(name)) {
			return { std::nullopt, ProxyData::Status::Unsupported };
		}
	}
	const auto encryption = LowerTrimmed(QueryValue(query, { u"encryption"_q }));
	if (!encryption.isEmpty() && encryption != u"none"_q) {
		return {};
	}
	if (HasNonEmptyQueryValue(query, { u"packetEncoding"_q, u"packetencoding"_q })
		|| HasNonEmptyQueryValue(query, { u"headerType"_q, u"headertype"_q })
		|| HasNonEmptyQueryValue(query, { u"seed"_q })
		|| HasNonEmptyQueryValue(query, { u"quicSecurity"_q, u"quicsecurity"_q })
		|| HasNonEmptyQueryValue(query, { u"key"_q })
		|| HasNonEmptyQueryValue(query, { u"multiMode"_q, u"multimode"_q })
		|| HasNonEmptyQueryValue(query, { u"sockopt"_q })
		|| HasNonEmptyQueryValue(query, { u"congestion"_q })
		|| HasNonEmptyQueryValue(query, { u"mux"_q, u"xmux"_q })) {
		return { std::nullopt, ProxyData::Status::Unsupported };
	}

	const auto transport = ParseVlessTransport(QueryValue(
		query,
		{ u"type"_q, u"transport"_q }));
	const auto security = ParseVlessSecurity(QueryValue(
		query,
		{ u"security"_q }));
	if (!transport || !security) {
		return { std::nullopt, ProxyData::Status::Unsupported };
	}

	auto proxy = ProxyData();
	proxy.type = ProxyData::Type::Vless;
	proxy.host = Trimmed(url.host());
	proxy.port = port;
	proxy.vless.id = url.userName();
	proxy.vless.transport = *transport;
	proxy.vless.security = *security;
	proxy.vless.serverName = DecodedQueryValue(
		query,
		{ u"sni"_q, u"serverName"_q, u"servername"_q });
	proxy.vless.alpn = ParseAlpn(QueryValue(query, { u"alpn"_q }));
	proxy.vless.allowInsecure = ParseBool(QueryValue(
		query,
		{ u"allowInsecure"_q, u"allowinsecure"_q }));
	proxy.vless.flow = DecodedQueryValue(query, { u"flow"_q });
	proxy.vless.fingerprint = DecodedQueryValue(
		query,
		{ u"fp"_q, u"fingerprint"_q });
	proxy.vless.publicKey = DecodedQueryValue(
		query,
		{ u"pbk"_q, u"publicKey"_q, u"publickey"_q, u"password"_q });
	proxy.vless.shortId = DecodedQueryValue(
		query,
		{ u"sid"_q, u"shortId"_q, u"shortid"_q });
	proxy.vless.spiderX = DecodedQueryValue(
		query,
		{ u"spx"_q, u"spiderX"_q, u"spiderx"_q });
	proxy.vless.path = DecodedQueryValue(query, { u"path"_q });
	proxy.vless.hostHeader = DecodedQueryValue(query, { u"host"_q });
	proxy.vless.mode = DecodedQueryValue(query, { u"mode"_q });
	proxy.vless.serviceName = DecodedQueryValue(
		query,
		{ u"serviceName"_q, u"servicename"_q });
	proxy.vless.authority = DecodedQueryValue(query, { u"authority"_q });
	proxy.vless = NormalizeVlessConfig(proxy.vless);

	const auto status = proxy.status();
	return (status == ProxyData::Status::Valid)
		? ParsedVlessLink{ std::make_optional(proxy), status }
		: ParsedVlessLink{ std::nullopt, status };
}

[[nodiscard]] bool IsHexMtprotoPassword(const QString &password) {
	const auto size = password.size();
	if (size < 32 || size % 2 == 1) {
		return false;
	}
	const auto bad = [](QChar ch) {
		const auto code = ch.unicode();
		return (code < 'a' || code > 'f')
			&& (code < 'A' || code > 'F')
			&& (code < '0' || code > '9');
	};
	const auto i = std::find_if(password.begin(), password.end(), bad);
	return (i == password.end());
}

[[nodiscard]] ProxyData::Status HexMtprotoPasswordStatus(
		const QString &password) {
	const auto size = password.size() / 2;
	const auto type1 = password[0].toLower();
	const auto type2 = password[1].toLower();
	const auto valid = (size == 16)
		|| (size == 17 && (type1 == 'd') && (type2 == 'd'))
		|| (size >= 21 && (type1 == 'e') && (type2 == 'e'));
	if (valid) {
		return ProxyData::Status::Valid;
	} else if (size < 16) {
		return ProxyData::Status::Invalid;
	}
	return ProxyData::Status::Unsupported;
}

[[nodiscard]] bytes::vector SecretFromHexMtprotoPassword(
		const QString &password) {
	Expects(password.size() % 2 == 0);

	const auto size = password.size() / 2;
	const auto fromHex = [](QChar ch) -> int {
		const auto code = int(ch.unicode());
		if (code >= '0' && code <= '9') {
			return (code - '0');
		} else if (code >= 'A' && code <= 'F') {
			return 10 + (code - 'A');
		} else if (ch >= 'a' && ch <= 'f') {
			return 10 + (code - 'a');
		}
		Unexpected("Code in ProxyData fromHex.");
	};
	auto result = bytes::vector(size);
	for (auto i = 0; i != size; ++i) {
		const auto high = fromHex(password[2 * i]);
		const auto low = fromHex(password[2 * i + 1]);
		if (high < 0 || low < 0) {
			return {};
		}
		result[i] = static_cast<bytes::type>(high * 16 + low);
	}
	return result;
}

[[nodiscard]] QStringView Base64UrlInner(const QString &password) {
	Expects(password.size() > 2);

	return base::StringViewMid(password, 0, [&] {
		auto result = password.size();
		for (auto i = 0; i != 2; ++i) {
			const auto prev = result - 1;
			if (password[prev] != '=') {
				break;
			}
			result = prev;
		}
		return result;
	}());
}

[[nodiscard]] bool IsBase64UrlMtprotoPassword(const QString &password) {
	const auto size = password.size();
	if (size < 22 || size % 4 == 1) {
		return false;
	}
	const auto bad = [](QChar ch) {
		const auto code = ch.unicode();
		return (code < 'a' || code > 'z')
			&& (code < 'A' || code > 'Z')
			&& (code < '0' || code > '9')
			&& (code != '_')
			&& (code != '-');
	};
	const auto inner = Base64UrlInner(password);
	const auto begin = inner.data();
	const auto end = begin + inner.size();
	return (std::find_if(begin, end, bad) == end);
}

[[nodiscard]] ProxyData::Status Base64UrlMtprotoPasswordStatus(
		const QString &password) {
	const auto inner = Base64UrlInner(password);
	const auto size = (inner.size() * 3) / 4;
	const auto valid = (size == 16)
		|| (size == 17
			&& (password[0] == '3')
			&& ((password[1] >= 'Q' && password[1] <= 'Z')
				|| (password[1] >= 'a' && password[1] <= 'f')))
		|| (size >= 21
			&& (password[0] == '7')
			&& (password[1] >= 'g')
			&& (password[1] <= 'v'));
	const auto incorrect = (size >= 21
		&& password[0].toLower() == 'e'
		&& password[1].toLower() == 'e');
	if (size < 16) {
		return ProxyData::Status::Invalid;
	} else if (valid) {
		return ProxyData::Status::Valid;
	} else if (incorrect) {
		return ProxyData::Status::IncorrectSecret;
	}
	return ProxyData::Status::Unsupported;
}

[[nodiscard]] bytes::vector SecretFromBase64UrlMtprotoPassword(
		const QString &password) {
	const auto result = QByteArray::fromBase64(
		password.toLatin1(),
		QByteArray::Base64UrlEncoding);
	return bytes::make_vector(bytes::make_span(result));
}

[[nodiscard]] bool EqualVlessConfig(
		const ProxyData::VlessConfig &left,
		const ProxyData::VlessConfig &right) {
	const auto normalizedLeft = NormalizeVlessConfig(left);
	const auto normalizedRight = NormalizeVlessConfig(right);
	return (normalizedLeft.id == normalizedRight.id)
		&& (normalizedLeft.transport == normalizedRight.transport)
		&& (normalizedLeft.security == normalizedRight.security)
		&& (normalizedLeft.serverName == normalizedRight.serverName)
		&& (normalizedLeft.alpn == normalizedRight.alpn)
		&& (normalizedLeft.allowInsecure == normalizedRight.allowInsecure)
		&& (normalizedLeft.flow == normalizedRight.flow)
		&& (normalizedLeft.fingerprint == normalizedRight.fingerprint)
		&& (normalizedLeft.publicKey == normalizedRight.publicKey)
		&& (normalizedLeft.shortId == normalizedRight.shortId)
		&& (normalizedLeft.spiderX == normalizedRight.spiderX)
		&& (normalizedLeft.path == normalizedRight.path)
		&& (normalizedLeft.hostHeader == normalizedRight.hostHeader)
		&& (normalizedLeft.mode == normalizedRight.mode)
		&& (normalizedLeft.serviceName == normalizedRight.serviceName)
		&& (normalizedLeft.authority == normalizedRight.authority);
}

} // namespace

bool ProxyData::valid() const {
	return status() == Status::Valid;
}

ProxyData::Status ProxyData::status() const {
	if (type == Type::None || host.isEmpty() || !port) {
		return Status::Invalid;
	} else if (type == Type::Mtproto) {
		return MtprotoPasswordStatus(password);
	} else if (type == Type::Vless) {
		return VlessStatus(host, NormalizeVlessConfig(vless));
	}
	return Status::Valid;
}

bool ProxyData::supportsCalls() const {
	return (type == Type::Vless) && VlessProxyForCallsEnabled();
}

bool ProxyData::tryCustomResolve() const {
	static const auto RegExp = QRegularExpression(
		QStringLiteral("^\\d+\\.\\d+\\.\\d+\\.\\d+$")
	);
	return (type == Type::Socks5 || type == Type::Mtproto)
		&& !qthelp::is_ipv6(host)
		&& !RegExp.match(host).hasMatch();
}

bytes::vector ProxyData::secretFromMtprotoPassword() const {
	Expects(type == Type::Mtproto);

	if (IsHexMtprotoPassword(password)) {
		return SecretFromHexMtprotoPassword(password);
	} else if (IsBase64UrlMtprotoPassword(password)) {
		return SecretFromBase64UrlMtprotoPassword(password);
	}
	return {};
}

ProxyData::operator bool() const {
	return valid();
}

bool ProxyData::operator==(const ProxyData &other) const {
	if (!valid()) {
		return !other.valid();
	}
	if (type == Type::Vless || other.type == Type::Vless) {
		return (type == other.type)
			&& (host == other.host)
			&& (port == other.port)
			&& EqualVlessConfig(vless, other.vless);
	}
	return (type == other.type)
		&& (host == other.host)
		&& (port == other.port)
		&& (user == other.user)
		&& (password == other.password);
}

bool ProxyData::operator!=(const ProxyData &other) const {
	return !(*this == other);
}

bool ProxyData::ValidMtprotoPassword(const QString &password) {
	return MtprotoPasswordStatus(password) == Status::Valid;
}

ProxyData::Status ProxyData::MtprotoPasswordStatus(const QString &password) {
	if (IsHexMtprotoPassword(password)) {
		return HexMtprotoPasswordStatus(password);
	} else if (IsBase64UrlMtprotoPassword(password)) {
		return Base64UrlMtprotoPasswordStatus(password);
	}
	return Status::Invalid;
}

ProxyData::Status ProxyData::VlessLinkStatus(const QString &link) {
	return ParseVlessLink(link).status;
}

std::optional<ProxyData> ProxyData::TryParseVlessLink(const QString &link) {
	return ParseVlessLink(link).proxy;
}

QString ProxyData::toVlessLink() const {
	if (type != Type::Vless) {
		return QString();
	}

	const auto config = NormalizeVlessConfig(vless);
	if (VlessStatus(host, config) != Status::Valid) {
		return QString();
	}

	auto url = QUrl();
	url.setScheme(u"vless"_q);
	url.setUserName(config.id);
	url.setHost(host);
	url.setPort(port);

	auto query = QUrlQuery();
	query.addQueryItem(u"encryption"_q, u"none"_q);
	if (config.transport != VlessTransport::Tcp) {
		query.addQueryItem(
			u"type"_q,
			SerializeVlessTransport(config.transport));
	}
	if (config.security != VlessSecurity::None) {
		query.addQueryItem(
			u"security"_q,
			SerializeVlessSecurity(config.security));
	}
	if (!config.serverName.isEmpty()) {
		query.addQueryItem(u"sni"_q, config.serverName);
	}
	if (!config.alpn.isEmpty()) {
		query.addQueryItem(u"alpn"_q, JoinList(config.alpn));
	}
	if (config.allowInsecure) {
		query.addQueryItem(u"allowInsecure"_q, u"1"_q);
	}
	if (!config.flow.isEmpty()) {
		query.addQueryItem(u"flow"_q, config.flow);
	}
	if (!config.fingerprint.isEmpty()) {
		query.addQueryItem(u"fp"_q, config.fingerprint);
	}
	if (!config.publicKey.isEmpty()) {
		query.addQueryItem(u"pbk"_q, config.publicKey);
	}
	if (!config.shortId.isEmpty()) {
		query.addQueryItem(u"sid"_q, config.shortId);
	}
	if (!config.spiderX.isEmpty()) {
		query.addQueryItem(u"spx"_q, config.spiderX);
	}
	if (!config.path.isEmpty()) {
		query.addQueryItem(u"path"_q, config.path);
	}
	if (!config.hostHeader.isEmpty()) {
		query.addQueryItem(u"host"_q, config.hostHeader);
	}
	if (!config.mode.isEmpty()) {
		query.addQueryItem(u"mode"_q, config.mode);
	}
	if (!config.serviceName.isEmpty()) {
		query.addQueryItem(u"serviceName"_q, config.serviceName);
	}
	if (!config.authority.isEmpty()) {
		query.addQueryItem(u"authority"_q, config.authority);
	}
	url.setQuery(query);
	return url.toString(QUrl::FullyEncoded);
}

ProxyData ToDirectIpProxy(const ProxyData &proxy, int ipIndex) {
	if (!proxy.tryCustomResolve()
		|| ipIndex < 0
		|| ipIndex >= proxy.resolvedIPs.size()) {
		return proxy;
	}
	return {
		proxy.type,
		proxy.resolvedIPs[ipIndex],
		proxy.port,
		proxy.user,
		proxy.password
	};
}

QNetworkProxy ToNetworkProxy(const ProxyData &proxy) {
	if (proxy.type == ProxyData::Type::None) {
		return QNetworkProxy::DefaultProxy;
	} else if (proxy.type == ProxyData::Type::Mtproto
		|| proxy.type == ProxyData::Type::Vless) {
		return QNetworkProxy::NoProxy;
	}
	return QNetworkProxy(
		(proxy.type == ProxyData::Type::Socks5
			? QNetworkProxy::Socks5Proxy
			: QNetworkProxy::HttpProxy),
		proxy.host,
		proxy.port,
		proxy.user,
		proxy.password);
}

bool VlessProxyForCallsEnabled() {
	return OptionVlessProxyForCalls.value();
}

} // namespace MTP
