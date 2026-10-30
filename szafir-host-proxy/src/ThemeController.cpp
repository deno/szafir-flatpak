#include "ThemeController.h"
#include "AppSettings.h"

#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QModelIndex>
#include <QStyleHints>

#include <KColorSchemeManager>
#include <KConfigGroup>
#include <KSharedConfig>

namespace {

QString themeName(ThemeController::Theme theme)
{
    switch (theme) {
    case ThemeController::Light:
        return QStringLiteral("Light");
    case ThemeController::Dark:
        return QStringLiteral("Dark");
    case ThemeController::System:
        break;
    }
    return QStringLiteral("System");
}

ThemeController::Theme themeFromName(const QString &name)
{
    if (name == QLatin1String("Light"))
        return ThemeController::Light;
    if (name == QLatin1String("Dark"))
        return ThemeController::Dark;
    return ThemeController::System;
}

} // namespace

ThemeController::ThemeController(QObject *parent)
    : QObject(parent)
{
    // We persist the choice ourselves in the app config.
    KColorSchemeManager::instance()->setAutosaveChanges(false);
    loadSettings();
    applyTheme();

    // Follow live system scheme changes while in System mode.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
        if (m_theme == System)
            applyTheme();
    });
}

void ThemeController::setTheme(Theme theme)
{
    if (m_theme == theme)
        return;
    m_theme = theme;
    saveSettings();
    applyTheme();
    Q_EMIT themeChanged();
}

void ThemeController::applyTheme()
{
    auto *manager = KColorSchemeManager::instance();

    Theme effective = m_theme;
    if (effective == System) {
        // kdeglobals carries only a scheme hash (no name), so KColorSchemeManager's
        // "default" resolves to light; derive the system scheme from QStyleHints.
        effective = (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
            ? Dark
            : Light;
    }

    // Match on the scheme file path (UserRole): unlike the display name it is
    // not localized, so the Breeze schemes resolve in any locale.
    const QString needle = (effective == Light)
        ? QStringLiteral("BreezeLight.colors")
        : QStringLiteral("BreezeDark.colors");

    QAbstractItemModel *model = manager->model();
    for (int i = 0; i < model->rowCount(); ++i) {
        const QModelIndex idx = model->index(i, 0);
        if (idx.data(Qt::UserRole).toString().endsWith(needle)) {
            manager->activateScheme(idx);
            return;
        }
    }

    manager->activateScheme(QModelIndex()); // scheme missing -> follow system
}

void ThemeController::loadSettings()
{
    const KConfigGroup group = KSharedConfig::openConfig(appConfigFileName())->group(QStringLiteral("Appearance"));
    m_theme = themeFromName(group.readEntry(QStringLiteral("Theme"), QStringLiteral("System")));
}

void ThemeController::saveSettings()
{
    KConfigGroup group = KSharedConfig::openConfig(appConfigFileName())->group(QStringLiteral("Appearance"));
    group.writeEntry(QStringLiteral("Theme"), themeName(m_theme));
    group.sync();
}
