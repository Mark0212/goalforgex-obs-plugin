/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-tiktok-relay.hpp"

#include "gfx-api.hpp"
#include "gfx-auth.hpp"
#include "gfx-http.hpp"

#include <curl/curl.h>

#include <obs-module.h>
#include <plugin-support.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QString>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/select.h>
#endif

namespace gfx {

namespace {

const int kRelayProtocol = 1;
const qsizetype kMaxFrameBytes = 16 * 1024 * 1024;
const qsizetype kMaxQueuedFrames = 2000;

// ── Only TikTok is ever contacted ──────────────────────────────────

bool endsWith(const std::string &s, const std::string &suffix)
{
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool tiktokHost(const std::string &hostIn)
{
	std::string host = hostIn;
	std::transform(host.begin(), host.end(), host.begin(),
		       [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
	const char *const roots[] = {"tiktok.com", "tiktokv.com",   "tiktokv.us",
				     "tiktokv.eu", "tiktokcdn.com", "tiktokcdn-us.com"};
	for (const char *root : roots) {
		const std::string r(root);
		if (host == r || endsWith(host, "." + r))
			return true;
	}
	return false;
}

struct Target {
	bool secure = true;
	std::string host;
	long port = 443;
	std::string resource = "/"; // path + query, exactly as given
};

// "wss://host[:port]/path?query" (or https:// for a live check). No user info, no IPv6 literals.
bool parseUrl(const std::string &url, Target &t, std::string &err)
{
	const std::size_t sep = url.find("://");
	if (sep == std::string::npos) {
		err = "bad URL";
		return false;
	}
	const std::string scheme = url.substr(0, sep);
	if (scheme == "wss" || scheme == "https") {
		t.secure = true;
		t.port = 443;
	} else {
		err = "only secure TikTok URLs are allowed";
		return false;
	}
	const std::size_t hostStart = sep + 3;
	std::size_t pathStart = url.find('/', hostStart);
	const std::size_t queryStart = url.find('?', hostStart);
	if (pathStart == std::string::npos || (queryStart != std::string::npos && queryStart < pathStart))
		pathStart = queryStart;
	std::string hostPort = pathStart == std::string::npos ? url.substr(hostStart)
							      : url.substr(hostStart, pathStart - hostStart);
	t.resource = pathStart == std::string::npos ? std::string("/") : url.substr(pathStart);
	if (!t.resource.empty() && t.resource[0] == '?')
		t.resource = "/" + t.resource;
	if (hostPort.find('@') != std::string::npos || hostPort.find('[') != std::string::npos) {
		err = "bad host";
		return false;
	}
	const std::size_t colon = hostPort.find(':');
	if (colon != std::string::npos) {
		const long p = std::strtol(hostPort.c_str() + colon + 1, nullptr, 10);
		if (p <= 0 || p > 65535) {
			err = "bad port";
			return false;
		}
		t.port = p;
		hostPort = hostPort.substr(0, colon);
	}
	t.host = hostPort;
	if (t.host.empty() || !tiktokHost(t.host)) {
		err = "not a TikTok address: " + t.host;
		return false;
	}
	for (char c : t.resource) {
		if (c == '\r' || c == '\n' || c == ' ') {
			err = "bad path";
			return false;
		}
	}
	return true;
}

bool safeHeader(const QString &name, const QString &value)
{
	if (name.isEmpty() || name.size() > 100 || value.size() > 8000)
		return false;
	for (const QChar c : name)
		if (!(c.isLetterOrNumber() || c == QLatin1Char('-')))
			return false;
	return !value.contains(QLatin1Char('\r')) && !value.contains(QLatin1Char('\n'));
}

// Aborts a curl transfer / connect once OBS is exiting.
int abortOnShutdown(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return httpShuttingDown() ? 1 : 0;
}

// ── Waiting on a raw socket (curl CONNECT_ONLY) ─────────────────────

bool waitSocket(curl_socket_t sock, bool forWrite, int timeoutMs)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET(sock, &set);
	struct timeval tv;
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
#ifdef _WIN32
	const int n = select(0, forWrite ? nullptr : &set, forWrite ? &set : nullptr, nullptr, &tv);
#else
	const int n =
		select(static_cast<int>(sock) + 1, forWrite ? nullptr : &set, forWrite ? &set : nullptr, nullptr, &tv);
#endif
	return n > 0;
}

// ── A small WebSocket client on top of curl's CONNECT_ONLY TLS connection ──
// OBS's own libcurl may be built without WebSocket support, but it always has
// TLS: curl connects + handshakes TLS, the HTTP upgrade and RFC 6455 framing are
// done here. One thread does all the socket I/O (a curl handle is single-threaded).

class WsClient {
public:
	std::function<void()> onOpen;
	std::function<void(const QByteArray &)> onFrame;
	std::function<void(int, const QString &)> onClosed; // exactly once

	void send(const QByteArray &data)
	{
		std::lock_guard<std::mutex> l(m_);
		if (static_cast<qsizetype>(out_.size()) < kMaxQueuedFrames)
			out_.push_back(data);
	}
	void requestClose() { closeReq_ = true; }

	void run(const std::string &url, const std::vector<std::string> &headers)
	{
		std::string err;
		Target t;
		if (!parseUrl(url, t, err))
			return finish(1006, QString::fromStdString(err));

		CURL *curl = curl_easy_init();
		if (!curl)
			return finish(1006, QStringLiteral("could not start a network request"));
		curl_ = curl;
		const std::string connectUrl =
			std::string(t.secure ? "https://" : "http://") + t.host + ":" + std::to_string(t.port) + "/";
		curl_easy_setopt(curl, CURLOPT_URL, connectUrl.c_str());
		curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abortOnShutdown);
		const CURLcode rc = curl_easy_perform(curl);
		if (rc != CURLE_OK)
			return finish(
				1006,
				QStringLiteral("connect failed: %1").arg(QString::fromUtf8(curl_easy_strerror(rc))));
		if (curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sock_) != CURLE_OK || sock_ == CURL_SOCKET_BAD)
			return finish(1006, QStringLiteral("no socket"));

		// HTTP/1.1 upgrade
		QByteArray keyRaw(16, '\0');
		for (qsizetype i = 0; i < keyRaw.size(); ++i)
			keyRaw[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
		const QByteArray key = keyRaw.toBase64();
		std::string req = "GET " + t.resource + " HTTP/1.1\r\nHost: " + t.host +
				  (t.port != 443 ? ":" + std::to_string(t.port) : std::string()) +
				  "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
				  "Sec-WebSocket-Key: " +
				  key.toStdString() + "\r\n";
		for (const std::string &h : headers)
			req += h + "\r\n";
		req += "\r\n";
		if (!sendAll(QByteArray::fromStdString(req)))
			return finish(1006, QStringLiteral("could not send the upgrade request"));

		QByteArray head;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
		qsizetype end = -1;
		while ((end = head.indexOf("\r\n\r\n")) < 0) {
			if (closeReq_ || httpShuttingDown())
				return finish(1000, QStringLiteral("closed"));
			if (std::chrono::steady_clock::now() > deadline || head.size() > 32 * 1024)
				return finish(1006, QStringLiteral("no upgrade answer from TikTok"));
			int got = recvSome(head);
			if (got < 0)
				return finish(1006, QStringLiteral("TikTok closed the connection during the upgrade"));
		}
		const QByteArray status = head.left(head.indexOf("\r\n"));
		if (!status.contains(" 101"))
			return finish(1006, QStringLiteral("TikTok refused the socket: %1")
						    .arg(QString::fromLatin1(status.left(80))));
		const QByteArray lower = head.left(end).toLower();
		const QByteArray expect =
			QCryptographicHash::hash(key + QByteArrayLiteral("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"),
						 QCryptographicHash::Sha1)
				.toBase64();
		const qsizetype ak = lower.indexOf("sec-websocket-accept:");
		if (ak < 0)
			return finish(1006, QStringLiteral("bad upgrade answer"));
		const qsizetype akEnd = lower.indexOf("\r\n", ak);
		// `lower` stops at the blank line, so the last header has no "\r\n" after it — end there,
		// never at head.size() (that would take in the first frame's bytes too).
		const QByteArray accept = head.mid(ak + 21, (akEnd < 0 ? end : akEnd) - ak - 21).trimmed();
		if (accept != expect)
			return finish(1006, QStringLiteral("bad upgrade answer (accept key)"));
		in_ = head.mid(end + 4);
		if (onOpen)
			onOpen();

		// frames
		for (;;) {
			if (httpShuttingDown())
				closeReq_ = true;
			if (closeReq_) {
				sendFrame(0x8, closePayload(1000));
				return finish(1000, QStringLiteral("closed"));
			}
			std::deque<QByteArray> batch;
			{
				std::lock_guard<std::mutex> l(m_);
				batch.swap(out_);
			}
			for (const QByteArray &b : batch)
				if (!sendFrame(0x2, b))
					return finish(1006, QStringLiteral("send failed"));
			int code = 0;
			QString reason;
			if (!parseFrames(code, reason))
				return finish(code ? code : 1006, reason);
			const int got = recvSome(in_);
			if (got < 0)
				return finish(1006, QStringLiteral("TikTok closed the connection"));
		}
	}

	~WsClient()
	{
		if (curl_)
			curl_easy_cleanup(curl_);
	}

private:
	void finish(int code, const QString &reason)
	{
		if (finished_)
			return;
		finished_ = true;
		if (onClosed)
			onClosed(code, reason);
	}

	// >0 bytes read · 0 nothing yet (waited a little) · <0 closed / error
	int recvSome(QByteArray &into)
	{
		char buf[65536];
		size_t n = 0;
		const CURLcode rc = curl_easy_recv(curl_, buf, sizeof(buf), &n);
		if (rc == CURLE_AGAIN) {
			waitSocket(sock_, false, 50);
			return 0;
		}
		if (rc != CURLE_OK || n == 0)
			return -1;
		into.append(buf, static_cast<qsizetype>(n));
		return static_cast<int>(n);
	}

	bool sendAll(const QByteArray &data)
	{
		qsizetype off = 0;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
		while (off < data.size()) {
			size_t n = 0;
			const CURLcode rc = curl_easy_send(curl_, data.constData() + off,
							   static_cast<size_t>(data.size() - off), &n);
			if (rc == CURLE_AGAIN) {
				if (std::chrono::steady_clock::now() > deadline || httpShuttingDown())
					return false;
				waitSocket(sock_, true, 100);
				continue;
			}
			if (rc != CURLE_OK)
				return false;
			off += static_cast<qsizetype>(n);
		}
		return true;
	}

	static QByteArray closePayload(int code)
	{
		QByteArray p(2, '\0');
		p[0] = static_cast<char>((code >> 8) & 0xFF);
		p[1] = static_cast<char>(code & 0xFF);
		return p;
	}

	// Client frames are always masked (RFC 6455 §5.3).
	bool sendFrame(int opcode, const QByteArray &payload)
	{
		QByteArray f;
		f.append(static_cast<char>(0x80 | (opcode & 0x0F)));
		const quint64 len = static_cast<quint64>(payload.size());
		if (len < 126) {
			f.append(static_cast<char>(0x80 | static_cast<int>(len)));
		} else if (len <= 0xFFFF) {
			f.append(static_cast<char>(0x80 | 126));
			f.append(static_cast<char>((len >> 8) & 0xFF));
			f.append(static_cast<char>(len & 0xFF));
		} else {
			f.append(static_cast<char>(0x80 | 127));
			for (int i = 7; i >= 0; --i)
				f.append(static_cast<char>((len >> (8 * i)) & 0xFF));
		}
		const quint32 maskWord = QRandomGenerator::global()->generate();
		char mask[4];
		for (int i = 0; i < 4; ++i)
			mask[i] = static_cast<char>((maskWord >> (8 * i)) & 0xFF);
		f.append(mask, 4);
		QByteArray body = payload;
		for (qsizetype i = 0; i < body.size(); ++i)
			body[i] = static_cast<char>(body[i] ^ mask[i % 4]);
		f.append(body);
		return sendAll(f);
	}

	// Pulls every complete frame out of in_. false = the connection is over (code/reason set).
	bool parseFrames(int &code, QString &reason)
	{
		for (;;) {
			if (in_.size() < 2)
				return true;
			const unsigned char b0 = static_cast<unsigned char>(in_[0]);
			const unsigned char b1 = static_cast<unsigned char>(in_[1]);
			const bool fin = (b0 & 0x80) != 0;
			const int op = b0 & 0x0F;
			const bool masked = (b1 & 0x80) != 0;
			quint64 len = b1 & 0x7F;
			qsizetype idx = 2;
			if (len == 126) {
				if (in_.size() < 4)
					return true;
				len = (static_cast<quint64>(static_cast<unsigned char>(in_[2])) << 8) |
				      static_cast<quint64>(static_cast<unsigned char>(in_[3]));
				idx = 4;
			} else if (len == 127) {
				if (in_.size() < 10)
					return true;
				len = 0;
				for (int i = 0; i < 8; ++i)
					len = (len << 8) | static_cast<quint64>(static_cast<unsigned char>(in_[2 + i]));
				idx = 10;
			}
			if (len > static_cast<quint64>(kMaxFrameBytes)) {
				code = 1009;
				reason = QStringLiteral("frame too big");
				return false;
			}
			char mask[4] = {0, 0, 0, 0};
			if (masked) {
				if (in_.size() < idx + 4)
					return true;
				for (int i = 0; i < 4; ++i)
					mask[i] = in_[idx + i];
				idx += 4;
			}
			const qsizetype plen = static_cast<qsizetype>(len);
			if (in_.size() < idx + plen)
				return true;
			QByteArray payload = in_.mid(idx, plen);
			in_.remove(0, idx + plen);
			if (masked)
				for (qsizetype i = 0; i < payload.size(); ++i)
					payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);

			if (op == 0x8) { // close
				int c = 1005;
				if (payload.size() >= 2)
					c = (static_cast<unsigned char>(payload[0]) << 8) |
					    static_cast<unsigned char>(payload[1]);
				sendFrame(0x8, closePayload(c == 1005 ? 1000 : c));
				code = c;
				reason = payload.size() > 2 ? QString::fromUtf8(payload.mid(2, 120))
							    : QStringLiteral("TikTok closed the socket");
				return false;
			}
			if (op == 0x9) { // ping → pong
				sendFrame(0xA, payload);
				continue;
			}
			if (op == 0xA)
				continue;
			if (op == 0x0) { // continuation
				frag_.append(payload);
				if (frag_.size() > kMaxFrameBytes) {
					code = 1009;
					reason = QStringLiteral("message too big");
					return false;
				}
				if (fin) {
					if (onFrame)
						onFrame(frag_);
					frag_.clear();
				}
				continue;
			}
			if (op == 0x1 || op == 0x2) {
				if (fin) {
					if (onFrame)
						onFrame(payload);
				} else {
					frag_ = payload;
				}
				continue;
			}
			code = 1002;
			reason = QStringLiteral("unknown frame type");
			return false;
		}
	}

	std::mutex m_;
	std::deque<QByteArray> out_;
	std::atomic<bool> closeReq_{false};
	bool finished_ = false;
	CURL *curl_ = nullptr;
	curl_socket_t sock_ = CURL_SOCKET_BAD;
	QByteArray in_;
	QByteArray frag_;
};

// ── Live check: one GET to TikTok from this PC ─────────────────────

size_t probeWrite(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *p = static_cast<std::pair<QByteArray *, qsizetype> *>(userdata);
	const size_t n = size * nmemb;
	if (p->first->size() + static_cast<qsizetype>(n) > p->second)
		return 0;
	p->first->append(ptr, static_cast<qsizetype>(n));
	return n;
}

QJsonObject runProbe(const QJsonObject &req)
{
	QJsonObject out;
	out.insert(QStringLiteral("id"), req.value(QStringLiteral("id")).toString());
	const std::string url = req.value(QStringLiteral("url")).toString().toStdString();
	Target t;
	std::string err;
	if (!parseUrl(url, t, err) || url.rfind("https://", 0) != 0) {
		out.insert(QStringLiteral("status"), 0);
		out.insert(QStringLiteral("error"), QString::fromStdString(err.empty() ? "not https" : err));
		return out;
	}
	const qsizetype maxBytes =
		std::clamp<qsizetype>(static_cast<qsizetype>(req.value(QStringLiteral("maxBytes")).toDouble(3e6)),
				      64 * 1024, 8 * 1024 * 1024);
	CURL *curl = curl_easy_init();
	if (!curl) {
		out.insert(QStringLiteral("status"), 0);
		out.insert(QStringLiteral("error"), QStringLiteral("could not start a network request"));
		return out;
	}
	QByteArray body;
	std::pair<QByteArray *, qsizetype> sink(&body, maxBytes);
	struct curl_slist *hdrs = nullptr;
	std::vector<std::string> keep;
	const QJsonObject h = req.value(QStringLiteral("headers")).toObject();
	for (auto it = h.begin(); it != h.end(); ++it) {
		const QString v = it.value().toString();
		if (!safeHeader(it.key(), v))
			continue;
		keep.push_back((it.key() + QStringLiteral(": ") + v).toStdString());
	}
	for (const std::string &line : keep)
		hdrs = curl_slist_append(hdrs, line.c_str());
	char errbuf[CURL_ERROR_SIZE] = {0};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // gzip / br as curl supports, decoded for us
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, probeWrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abortOnShutdown);
	const CURLcode rc = curl_easy_perform(curl);
	long status = 0;
	if (rc == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_slist_free_all(hdrs);
	curl_easy_cleanup(curl);
	out.insert(QStringLiteral("status"), static_cast<int>(status));
	if (rc != CURLE_OK)
		out.insert(QStringLiteral("error"),
			   QString::fromUtf8(errbuf[0] ? errbuf : curl_easy_strerror(rc)).left(200));
	else
		out.insert(QStringLiteral("body"), QString::fromUtf8(body));
	return out;
}

// ── The relay worker ───────────────────────────────────────────────

class Relay {
public:
	void start()
	{
		std::lock_guard<std::mutex> l(m_);
		if (worker_.joinable())
			return;
		stop_ = false;
		worker_ = std::thread([this] { loop(); });
	}

	void stop()
	{
		{
			std::lock_guard<std::mutex> l(m_);
			stop_ = true;
		}
		cv_.notify_all();
		if (worker_.joinable())
			worker_.join();
	}

private:
	// What the socket thread hands back to the worker.
	struct Pending {
		bool opened = false;
		bool closed = false;
		int code = 0;
		QString reason;
		std::vector<QByteArray> frames;
	};

	bool stopping()
	{
		std::lock_guard<std::mutex> l(m_);
		return stop_ || httpShuttingDown();
	}

	// Sleeps up to ms; wakes early for stop, or (batching ~100 ms) when the socket has news.
	void wait(int ms)
	{
		std::unique_lock<std::mutex> l(m_);
		const bool woke = cv_.wait_for(l, std::chrono::milliseconds(ms), [this] {
			return stop_ || pending_.opened || pending_.closed || !pending_.frames.empty();
		});
		if (woke && !stop_ && !pending_.closed && !pending_.opened)
			cv_.wait_for(l, std::chrono::milliseconds(100), [this] { return stop_; });
	}

	void closeSocket()
	{
		std::shared_ptr<WsClient> ws;
		std::thread th;
		{
			std::lock_guard<std::mutex> l(m_);
			ws = ws_;
			ws_.reset();
			th.swap(wsThread_);
			sid_.clear();
			pending_ = Pending();
		}
		if (ws)
			ws->requestClose();
		if (th.joinable())
			th.join();
	}

	void openSocket(const QJsonObject &s)
	{
		closeSocket();
		const QString sid = s.value(QStringLiteral("sid")).toString();
		const std::string url = s.value(QStringLiteral("url")).toString().toStdString();
		std::vector<std::string> headers;
		const QJsonObject h = s.value(QStringLiteral("headers")).toObject();
		for (auto it = h.begin(); it != h.end(); ++it) {
			const QString v = it.value().toString();
			const QString k = it.key();
			// The upgrade's own headers are ours to write.
			if (!safeHeader(k, v) || k.compare(QLatin1String("Host"), Qt::CaseInsensitive) == 0 ||
			    k.startsWith(QLatin1String("Sec-WebSocket"), Qt::CaseInsensitive) ||
			    k.compare(QLatin1String("Upgrade"), Qt::CaseInsensitive) == 0 ||
			    k.compare(QLatin1String("Connection"), Qt::CaseInsensitive) == 0)
				continue;
			headers.push_back((k + QStringLiteral(": ") + v).toStdString());
		}
		auto ws = std::make_shared<WsClient>();
		ws->onOpen = [this, sid] {
			std::lock_guard<std::mutex> l(m_);
			if (sid_ == sid)
				pending_.opened = true;
			cv_.notify_all();
		};
		ws->onFrame = [this, sid](const QByteArray &f) {
			std::lock_guard<std::mutex> l(m_);
			if (sid_ == sid && static_cast<qsizetype>(pending_.frames.size()) < kMaxQueuedFrames)
				pending_.frames.push_back(f);
			cv_.notify_all();
		};
		ws->onClosed = [this, sid](int code, const QString &reason) {
			std::lock_guard<std::mutex> l(m_);
			if (sid_ == sid) {
				pending_.closed = true;
				pending_.code = code;
				pending_.reason = reason;
			}
			cv_.notify_all();
		};
		std::lock_guard<std::mutex> l(m_);
		sid_ = sid;
		pending_ = Pending();
		ws_ = ws;
		wsThread_ = std::thread([ws, url, headers] { ws->run(url, headers); });
		obs_log(LOG_INFO, "TikTok LIVE: connecting through this PC");
	}

	void loop()
	{
		int failWait = 5000;
		QJsonObject probeResult;
		bool haveProbe = false;
		while (!stopping()) {
			if (!Auth::instance().connected()) {
				closeSocket();
				wait(15000);
				continue;
			}
			QJsonObject body;
			body.insert(QStringLiteral("v"), kRelayProtocol);
			body.insert(QStringLiteral("client"),
				    QStringLiteral("obs-plugin/") + QString::fromUtf8(PLUGIN_VERSION));
			Pending sent;
			QString sentSid;
			{
				std::lock_guard<std::mutex> l(m_);
				sentSid = sid_;
				sent = pending_;
				pending_ = Pending();
				if (sent.closed)
					sid_.clear(); // reported below; the socket thread has finished
			}
			if (!sentSid.isEmpty())
				body.insert(QStringLiteral("sid"), sentSid);
			if (sent.opened)
				body.insert(QStringLiteral("opened"), true);
			if (!sent.frames.empty()) {
				QJsonArray a;
				for (const QByteArray &f : sent.frames)
					a.append(QString::fromLatin1(f.toBase64()));
				body.insert(QStringLiteral("frames"), a);
			}
			if (sent.closed) {
				QJsonObject c;
				c.insert(QStringLiteral("code"), sent.code);
				c.insert(QStringLiteral("reason"), sent.reason.left(160));
				body.insert(QStringLiteral("closed"), c);
				obs_log(LOG_INFO, "TikTok LIVE socket on this PC closed (%d): %s", sent.code,
					sent.reason.left(160).toUtf8().constData());
			}
			if (haveProbe) {
				body.insert(QStringLiteral("probe"), probeResult);
				haveProbe = false;
			}

			const ApiResult r = apiPost(QStringLiteral("/api/obs/tiktok/relay"), body);
			if (stopping())
				break;
			if (!r.ok()) {
				// Keep what the socket gave us for the next try (not the probe — it's stale by then).
				{
					std::lock_guard<std::mutex> l(m_);
					if (!sent.frames.empty())
						pending_.frames.insert(pending_.frames.begin(), sent.frames.begin(),
								       sent.frames.end());
					if (sent.opened)
						pending_.opened = true;
					if (sent.closed && !pending_.closed) {
						pending_.closed = true;
						pending_.code = sent.code;
						pending_.reason = sent.reason;
						sid_ = sentSid;
					}
					if (static_cast<qsizetype>(pending_.frames.size()) > kMaxQueuedFrames)
						pending_.frames.erase(
							pending_.frames.begin(),
							pending_.frames.begin() +
								static_cast<std::ptrdiff_t>(
									pending_.frames.size() -
									static_cast<size_t>(kMaxQueuedFrames)));
				}
				if (r.http.status == 404 || r.http.status == 426) {
					closeSocket(); // a site without the relay (or a plugin it no longer accepts)
					wait(10 * 60 * 1000);
				} else if (r.token == TokenStatus::NeedsReconnect) {
					closeSocket();
					wait(60 * 1000);
				} else {
					wait(failWait);
					failWait = std::min(failWait * 2, 60 * 1000);
				}
				continue;
			}
			failWait = 5000;
			if (sent.closed)
				closeSocket(); // reported — join the finished socket thread
			const QJsonObject o = r.http.json();
			const QString closeSid = o.value(QStringLiteral("close")).toString();
			{
				bool doClose = false;
				{
					std::lock_guard<std::mutex> l(m_);
					doClose = (!closeSid.isEmpty() && closeSid == sid_) ||
						  (!o.value(QStringLiteral("want")).toBool() && ws_);
				}
				if (doClose)
					closeSocket();
			}
			const QJsonObject session = o.value(QStringLiteral("session")).toObject();
			if (!session.isEmpty() && !session.value(QStringLiteral("sid")).toString().isEmpty())
				openSocket(session);
			const QJsonArray toTikTok = o.value(QStringLiteral("send")).toArray();
			if (!toTikTok.isEmpty()) {
				std::shared_ptr<WsClient> ws;
				{
					std::lock_guard<std::mutex> l(m_);
					ws = ws_;
				}
				if (ws)
					for (qsizetype i = 0; i < toTikTok.size(); ++i)
						ws->send(QByteArray::fromBase64(toTikTok.at(i).toString().toLatin1()));
			}
			const QJsonObject probe = o.value(QStringLiteral("probe")).toObject();
			if (!probe.isEmpty()) {
				probeResult = runProbe(probe);
				haveProbe = true;
				continue; // report it straight away
			}
			const int next = std::clamp(o.value(QStringLiteral("next")).toInt(5000), 200, 10 * 60 * 1000);
			wait(next);
		}
		closeSocket();
	}

	std::mutex m_;
	std::condition_variable cv_;
	bool stop_ = false;
	std::thread worker_;
	std::shared_ptr<WsClient> ws_;
	std::thread wsThread_;
	QString sid_;
	Pending pending_;
};

Relay &relay()
{
	static Relay r;
	return r;
}

} // namespace

void tiktokRelayStart()
{
	relay().start();
}

void tiktokRelayStop()
{
	relay().stop();
}

} // namespace gfx
