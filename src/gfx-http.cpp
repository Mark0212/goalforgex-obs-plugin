/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-http.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <curl/curl.h>

#include <QFile>
#include <QJsonDocument>
#include <QUrl>

#include <atomic>
#include <mutex>
#include <string>

namespace gfx {

static std::atomic<bool> g_shutdown{false};
static std::atomic<int> g_inflight{0};
static std::once_flag g_curlInit;

QJsonObject HttpResult::json() const
{
	const QJsonDocument doc = QJsonDocument::fromJson(body);
	return doc.isObject() ? doc.object() : QJsonObject();
}

QString HttpResult::errorCode() const
{
	return json().value(QStringLiteral("error")).toString();
}

QString HttpResult::serverMessage() const
{
	const QJsonObject o = json();
	const QString m = o.value(QStringLiteral("message")).toString();
	if (!m.isEmpty())
		return m;
	// Some site routes put the human text in `error` itself.
	const QString e = o.value(QStringLiteral("error")).toString();
	return e.contains(QLatin1Char(' ')) ? e : QString();
}

void httpBeginShutdown()
{
	g_shutdown = true;
}

bool httpShuttingDown()
{
	return g_shutdown.load();
}

int httpInFlight()
{
	return g_inflight.load();
}

QString baseUrl()
{
	const QString env = qEnvironmentVariable("GOALFORGEX_BASE_URL").trimmed();
	if (!env.isEmpty()) {
		const QUrl u(env);
		// https anywhere, or plain http only for a local dev server.
		const bool local = u.host() == QLatin1String("localhost") || u.host() == QLatin1String("127.0.0.1");
		if (u.isValid() &&
		    (u.scheme() == QLatin1String("https") || (u.scheme() == QLatin1String("http") && local))) {
			QString s = env;
			while (s.endsWith(QLatin1Char('/')))
				s.chop(1);
			return s;
		}
		obs_log(LOG_WARNING, "Ignoring invalid GOALFORGEX_BASE_URL (must be https, or http://localhost)");
	}
	return QStringLiteral("https://goalforgex.com");
}

static size_t writeCb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *buf = static_cast<QByteArray *>(userdata);
	const size_t n = size * nmemb;
	// Nothing the API returns is anywhere near this; refuse runaway bodies.
	if (static_cast<size_t>(buf->size()) + n > 4u * 1024u * 1024u)
		return 0;
	buf->append(ptr, static_cast<qsizetype>(n));
	return n;
}

static int progressCb(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return g_shutdown.load() ? 1 : 0; // non-zero aborts the transfer
}

static HttpResult perform(const char *method, const QString &url, const QByteArray *jsonBody, const QString &bearer)
{
	HttpResult r;
	if (g_shutdown.load()) {
		r.transportError = QStringLiteral("OBS is shutting down");
		return r;
	}
	std::call_once(g_curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

	struct InFlight {
		InFlight() { ++g_inflight; }
		~InFlight() { --g_inflight; }
	} guard;

	CURL *curl = curl_easy_init();
	if (!curl) {
		r.transportError = QStringLiteral("Could not start a network request");
		return r;
	}

	const std::string urlStr = url.toStdString();
	const std::string ua =
		std::string("GoalForgeX-OBS/") + PLUGIN_VERSION + " (OBS " + obs_get_version_string() + ")";
	char errbuf[CURL_ERROR_SIZE] = {0};

	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Accept: application/json");
	if (jsonBody)
		headers = curl_slist_append(headers, "Content-Type: application/json");
	std::string auth;
	if (!bearer.isEmpty()) {
		auth = "Authorization: Bearer " + bearer.toStdString();
		headers = curl_slist_append(headers, auth.c_str());
	}

	curl_easy_setopt(curl, CURLOPT_URL, urlStr.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, ua.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); // never follow a redirect with a bearer token attached
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCb);
	if (jsonBody) {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody->constData());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody->size()));
	}

	const CURLcode rc = curl_easy_perform(curl);
	if (rc == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
	} else {
		r.status = 0;
		r.transportError = QString::fromUtf8(errbuf[0] ? errbuf : curl_easy_strerror(rc));
		obs_log(LOG_WARNING, "%s %s failed: %s", method, urlStr.c_str(), r.transportError.toUtf8().constData());
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return r;
}

HttpResult httpGet(const QString &url, const QString &bearer)
{
	return perform("GET", url, nullptr, bearer);
}

HttpResult httpPostJson(const QString &url, const QJsonObject &body, const QString &bearer)
{
	const QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
	return perform("POST", url, &data, bearer);
}

static size_t fileWriteCb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *f = static_cast<QFile *>(userdata);
	const size_t n = size * nmemb;
	// An installer is a few MB; refuse anything absurd.
	if (static_cast<size_t>(f->size()) + n > 200u * 1024u * 1024u)
		return 0;
	return f->write(ptr, static_cast<qint64>(n)) == static_cast<qint64>(n) ? n : 0;
}

bool httpDownload(const QString &url, const QString &path, QString *error)
{
	if (g_shutdown.load()) {
		if (error)
			*error = QStringLiteral("OBS is shutting down");
		return false;
	}
	std::call_once(g_curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
	struct InFlight {
		InFlight() { ++g_inflight; }
		~InFlight() { --g_inflight; }
	} guard;

	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if (error)
			*error = QStringLiteral("Couldn't write the update to disk.");
		return false;
	}
	CURL *curl = curl_easy_init();
	if (!curl) {
		if (error)
			*error = QStringLiteral("Could not start a network request");
		return false;
	}
	const std::string urlStr = url.toStdString();
	const std::string ua =
		std::string("GoalForgeX-OBS/") + PLUGIN_VERSION + " (OBS " + obs_get_version_string() + ")";
	char errbuf[CURL_ERROR_SIZE] = {0};
	curl_easy_setopt(curl, CURLOPT_URL, urlStr.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, ua.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWriteCb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &f);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCb);
	const CURLcode rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	f.close();
	if (rc != CURLE_OK) {
		QFile::remove(path);
		if (error)
			*error = QStringLiteral("Download failed: %1")
					 .arg(QString::fromUtf8(errbuf[0] ? errbuf : curl_easy_strerror(rc)));
		obs_log(LOG_WARNING, "Update download failed: %s", errbuf[0] ? errbuf : curl_easy_strerror(rc));
		return false;
	}
	return true;
}

} // namespace gfx
