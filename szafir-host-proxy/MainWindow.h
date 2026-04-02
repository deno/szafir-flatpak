#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>

class HostRuntimeController;
class ScalingController;
class SetupController;
class NativeMessagingService;
class QQmlApplicationEngine;
class QWindow;
#ifdef BUNDLED_HOST
class ComponentDownloader;
#endif

class MainWindow : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int activeHostCount READ activeHostCount WRITE setActiveHostCount NOTIFY activeHostCountChanged)
    Q_PROPERTY(QVariantList clients READ clients NOTIFY clientsChanged)

public:
    explicit MainWindow(NativeMessagingService *service, ScalingController *scalingController,
                        SetupController *setupController,
#ifdef BUNDLED_HOST
                        ComponentDownloader *componentDownloader,
#endif
                        QObject *parent = nullptr);
    ~MainWindow() override;

    int activeHostCount() const;
    QVariantList clients() const;

    void show();
    void raise();
    void activateWindow();
    void hide();

#ifndef BUNDLED_HOST
    void setMissingHostOrigin(const QString &origin) { m_missingHostOrigin = origin; }
#endif

    Q_INVOKABLE void stopClient(qint64 pid);

public slots:
    void setActiveHostCount(int activeHostCount);

signals:
    void activeHostCountChanged(int activeHostCount);
    void clientsChanged();

private:
    void ensureWindow();

    HostRuntimeController *m_hostRuntime = nullptr;
    NativeMessagingService *m_service;
    ScalingController *m_scalingController;
    SetupController *m_setupController;
#ifdef BUNDLED_HOST
    ComponentDownloader *m_componentDownloader;
#endif
#ifndef BUNDLED_HOST
    QString m_missingHostOrigin;
#endif
    int m_activeHostCount = 0;
    QQmlApplicationEngine *m_engine = nullptr;
    QPointer<QWindow> m_window;
};
