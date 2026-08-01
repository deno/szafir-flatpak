#include "ThemeController.h"
#include "PathUtils.h"

#include <QAbstractItemModel>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QModelIndex>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStyleHints>

#include <KColorSchemeManager>

#include <filesystem>

namespace {

std::filesystem::path themeSettingsPath()
{
    const QString configHomeEnv = qEnvironmentVariable("XDG_CONFIG_HOME");
    const std::filesystem::path configHome = configHomeEnv.isEmpty()
        ? PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        : PathUtils::toFsPath(configHomeEnv);
    return configHome / "szafir-host-proxy" / "theme-settings.json";
}

} // namespace

ThemeController::ThemeController(QObject *parent)
    : QObject(parent)
{
    // We persist the choice ourselves in theme-settings.json.
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
    QFile f(PathUtils::toQString(themeSettingsPath()));
    if (!f.open(QIODevice::ReadOnly))
        return;

    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    m_theme = static_cast<Theme>(obj[QStringLiteral("theme")].toInt(System));
}

void ThemeController::saveSettings()
{
    QJsonObject obj;
    obj[QStringLiteral("theme")] = static_cast<int>(m_theme);

    std::error_code ec;
    std::filesystem::create_directories(themeSettingsPath().parent_path(), ec);
    QSaveFile f(PathUtils::toQString(themeSettingsPath()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson());
        f.commit();
    }
}
