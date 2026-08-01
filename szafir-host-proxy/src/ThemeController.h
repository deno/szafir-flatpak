#pragma once

#include <QObject>

class ThemeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Theme theme READ theme WRITE setTheme NOTIFY themeChanged)

public:
    enum Theme {
        System,
        Light,
        Dark
    };
    Q_ENUM(Theme)

    explicit ThemeController(QObject *parent = nullptr);

    Theme theme() const { return m_theme; }
    Q_INVOKABLE void setTheme(Theme theme);

Q_SIGNALS:
    void themeChanged();

private:
    void loadSettings();
    void saveSettings();
    void applyTheme();

    Theme m_theme = System;
};
