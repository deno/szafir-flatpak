#pragma once

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QUrl>
#include <QDebug>

namespace SecureNetwork {

inline constexpr const char *kAllowedHosts[] = {
    "www.elektronicznypodpis.pl",
    "storage.googleapis.com",
};

inline bool isAllowedHost(const QUrl &url)
{
    const QString host = url.host();
    for (const char *allowed : kAllowedHosts) {
        if (host == QLatin1String(allowed))
            return true;
    }
    return false;
}

// cdn.elektronicznypodpis.pl is a storage.googleapis.com bucket alias whose
// TLS endpoint serves only the leaf certificate (missing intermediate), so
// strict verification fails. Fetch the same object through the canonical GCS
// endpoint, which presents a complete, verifiable chain.
inline QUrl rewriteRedirectTarget(const QUrl &url)
{
    if (url.host() == QLatin1String("cdn.elektronicznypodpis.pl")) {
        QUrl rewritten(url);
        rewritten.setHost(QStringLiteral("storage.googleapis.com"));
        rewritten.setPath(QStringLiteral("/cdn.elektronicznypodpis.pl") + url.path());
        return rewritten;
    }
    return url;
}

inline QNetworkRequest makeSecureRequest(const QUrl &url)
{
    if (url.scheme() != QLatin1String("https")) {
        qCritical() << "SecureNetwork: refusing non-HTTPS URL:" << url.toString();
        return QNetworkRequest{};
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QSslConfiguration sslCfg = QSslConfiguration::defaultConfiguration();
    sslCfg.setPeerVerifyMode(QSslSocket::VerifyPeer);
    request.setSslConfiguration(sslCfg);

    return request;
}

// For downloads whose redirect targets must be re-validated (and possibly
// rewritten) per hop by the caller.
inline QNetworkRequest makeManualRedirectRequest(const QUrl &url)
{
    QNetworkRequest request = makeSecureRequest(url);
    if (request.url().isValid())
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
    return request;
}

inline void attachSslAbort(QNetworkReply *reply)
{
    QObject::connect(reply, &QNetworkReply::sslErrors, reply,
        [reply](const QList<QSslError> &errors) {
            for (const QSslError &e : errors)
                qWarning() << "TLS error (aborting):" << e.errorString();
            reply->abort();
        });

    QObject::connect(reply, &QNetworkReply::redirected, reply,
        [reply](const QUrl &target) {
            if (target.scheme() != QLatin1String("https")) {
                qWarning() << "Redirect to non-HTTPS target (aborting):" << target.toString();
                reply->abort();
            }
        });
}

} // namespace SecureNetwork
