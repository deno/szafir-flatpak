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
