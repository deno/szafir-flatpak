#pragma once

#include <QString>
#include <QUrl>

struct DiscoveredComponent {
    QUrl url;
    QString urlHash;
    QString version;
    QString filename;
    bool valid = false;
};

struct DiscoveryResult {
    DiscoveredComponent runtime;
    DiscoveredComponent library;
};

constexpr const char *kDiscoveryUrl = "https://www.elektronicznypodpis.pl/aplikacje-i-sterowniki";

DiscoveryResult parseDiscoveryPage(const QByteArray &html);
