#pragma once

#include <QObject>
#include <QString>

#include <filesystem>

class ScalingController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString szafirScale READ szafirScale NOTIFY szafirScaleChanged)
    Q_PROPERTY(QString hostScale READ hostScale NOTIFY hostScaleChanged)

public:
    explicit ScalingController(
        const std::filesystem::path &szafirOverridePath,
        const std::filesystem::path &hostOverridePath,
        QObject *parent = nullptr);

    QString szafirScale() const;
    QString hostScale() const;

    Q_INVOKABLE void setSzafirScale(const QString &scale);
    Q_INVOKABLE void setHostScale(const QString &scale);

signals:
    void szafirScaleChanged(const QString &scale);
    void hostScaleChanged(const QString &scale);

private:
    void reloadScales();
    static QString readScale(const std::filesystem::path &overridePath);
    static void writeScale(const std::filesystem::path &overridePath, const QString &scale);

    std::filesystem::path m_szafirOverridePath;
    std::filesystem::path m_hostOverridePath;
    QString m_szafirScale;
    QString m_hostScale;
};
