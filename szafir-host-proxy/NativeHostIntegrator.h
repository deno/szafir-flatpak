#pragma once

#include <QString>

class NativeHostIntegrator
{
public:
    explicit NativeHostIntegrator(bool dryRun = false);

    // Installs wrappers for any browser whose installed TEMPLATE_VERSION is
    // older than the current build. Safe to call on every launch.
    bool installIfNeeded();

    // Installs all wrappers unconditionally.
    bool installNow();

    // Removes only Szafir-managed artifacts and overrides.
    bool uninstall();

    bool isDryRun() const { return m_dryRun; }

private:
    bool installAll(bool force);
    bool removeAll();
    bool ensureWrapperTemplateLoaded();

    QString m_wrapperTemplate;
    bool m_dryRun;
};
