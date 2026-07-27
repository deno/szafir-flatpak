#include "ComponentDiscovery.h"

#include <QRegularExpression>

DiscoveryResult parseDiscoveryPage(const QByteArray &html)
{
    DiscoveryResult result;
    const QString page = QString::fromUtf8(html);

    // --- Runtime (szafirhost-install.jar) ---
    static const QRegularExpression runtimeLinkRe(
        QStringLiteral(R"re(href\s*=\s*"(https?://[^"]*/([0-9a-fA-F]{32})/szafirhost-install\.jar)")re"));

    struct Candidate {
        QUrl url;
        QString hash;
        int pos;
    };
    QList<Candidate> candidates;

    {
        QRegularExpressionMatchIterator it = runtimeLinkRe.globalMatch(page);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            QUrl url(m.captured(1));
            if (url.scheme() != QLatin1String("https"))
                continue;
            if (url.host() != QLatin1String("www.elektronicznypodpis.pl"))
                continue;
            candidates.append({url, m.captured(2), static_cast<int>(m.capturedStart())});
        }
    }

    if (!candidates.isEmpty()) {
        const Candidate *best = &candidates.first();
        for (const Candidate &c : candidates) {
            int ctxStart = qMax(0, c.pos - 400);
            int ctxEnd = qMin(html.size(), c.pos + 400);
            QString ctx = QString::fromUtf8(html.mid(ctxStart, ctxEnd - ctxStart)).toLower();
            if (ctx.contains(QLatin1String("macos")) || ctx.contains(QLatin1String("linux"))) {
                best = &c;
                break;
            }
        }

        result.runtime.url = best->url;
        result.runtime.urlHash = best->hash;
        result.runtime.filename = best->url.fileName();
        result.runtime.valid = true;

        int ctxStart = qMax(0, best->pos - 400);
        int ctxEnd = qMin(html.size(), best->pos + 400);
        QString ctx = QString::fromUtf8(html.mid(ctxStart, ctxEnd - ctxStart));

        static const QRegularExpression versionRe(
            QStringLiteral(R"(wersja\s+([0-9]+(?:\.[0-9]+)+))"), QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch vm = versionRe.match(ctx);
        if (vm.hasMatch())
            result.runtime.version = vm.captured(1);
    }

    // --- Library (libCCGraphiteP*.so) ---
    static const QRegularExpression libLinkRe(
        QStringLiteral(R"re(href\s*=\s*"(https?://[^"]*/([0-9a-fA-F]{32})/(libCCGraphiteP(?:[0-9]+(?:\.[0-9]+)+)\.so))")re"));

    {
        QRegularExpressionMatchIterator it = libLinkRe.globalMatch(page);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            QUrl url(m.captured(1));
            if (url.scheme() != QLatin1String("https"))
                continue;
            if (url.host() != QLatin1String("www.elektronicznypodpis.pl"))
                continue;

            result.library.url = url;
            result.library.urlHash = m.captured(2);
            result.library.filename = m.captured(3);
            result.library.valid = true;

            static const QRegularExpression libVersionRe(
                QStringLiteral(R"(libCCGraphiteP((?:[0-9]+\.)+[0-9]+)\.so)"));
            QRegularExpressionMatch vm = libVersionRe.match(m.captured(3));
            if (vm.hasMatch())
                result.library.version = vm.captured(1);

            break;
        }
    }

    return result;
}
