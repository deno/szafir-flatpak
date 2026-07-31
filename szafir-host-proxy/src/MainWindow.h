#pragma once

#include "NativeMessagingService.h"

#include <QObject>
#include <QPointer>

class HostRuntimeController;
class ScalingController;
class SetupController;
class NativeMessagingService;
class QQmlApplicationEngine;
class QWindow;
class ComponentDownloader;
class UpdateController;
class SmartCardMonitor;

class MainWindow : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int activeHostCount READ activeHostCount WRITE setActiveHostCount NOTIFY activeHostCountChanged)
    Q_PROPERTY(NativeMessagingService* clientsModel READ clientsModel CONSTANT)

public:
    explicit MainWindow(NativeMessagingService *service, ScalingController *scalingController,
                        SetupController *setupController,
                        ComponentDownloader *componentDownloader,
                        UpdateController *updateController,
                        SmartCardMonitor *smartCardMonitor,
                        QObject *parent = nullptr);
    ~MainWindow() override;

    int activeHostCount() const;
    NativeMessagingService *clientsModel() const;

    void show();
    void raise();
    void activateWindow();
    void hide();

    Q_INVOKABLE void stopClient(qint64 pid);

public slots:
    void setActiveHostCount(int activeHostCount);

signals:
    void activeHostCountChanged(int activeHostCount);

private:
    void ensureWindow();

    HostRuntimeController *m_hostRuntime = nullptr;
    NativeMessagingService *m_service;
    ScalingController *m_scalingController;
    SetupController *m_setupController;
    ComponentDownloader *m_componentDownloader;
    UpdateController *m_updateController;
    SmartCardMonitor *m_smartCardMonitor;
    int m_activeHostCount = 0;
    QQmlApplicationEngine *m_engine = nullptr;
    QPointer<QWindow> m_window;
};
