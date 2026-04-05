#include "LandlockSandbox.h"
#include "config.h"
#include "generated_permissions.h"
#include "LandlockFlags.h"

#include <QDebug>
#include <QStandardPaths>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <ranges>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/landlock.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;
using namespace Landlock;

// ---- Landlock syscall wrappers (no libc wrappers exist) --------------------

int landlockCreateRuleset(const struct landlock_ruleset_attr *attr, size_t size, __u32 flags)
{
    return static_cast<int>(syscall(__NR_landlock_create_ruleset, attr, size, flags));
}

int landlockAddRule(int rulesetFd, enum landlock_rule_type ruleType, const void *ruleAttr, __u32 flags)
{
    return static_cast<int>(syscall(__NR_landlock_add_rule, rulesetFd, ruleType, ruleAttr, flags));
}

int landlockRestrictSelf(int rulesetFd, __u32 flags)
{
    return static_cast<int>(syscall(__NR_landlock_restrict_self, rulesetFd, flags));
}

struct PathRule {
    std::string path;
    __u64 access;
};

// ---- Helpers ---------------------------------------------------------------

std::string homePath()
{
    const char *home = getenv("HOME");
    return home ? std::string(home) : "/";
}

std::string xdgConfigHome()
{
    const char *env = getenv("XDG_CONFIG_HOME");
    if (env && env[0] != '\0') return std::string(env);
    return homePath() + "/.config";
}

// Signal-safe rule helper for use in forked child (no Qt/C++ allocations).
bool addRuleRaw(int rulesetFd, const char *path, __u64 access, __u64 handled)
{
    const __u64 effective = access & handled;
    if (effective == 0)
        return true;

    int fd = open(path, O_PATH | O_CLOEXEC);
    if (fd < 0)
        return (errno == ENOENT || errno == ENOTDIR);

    struct landlock_path_beneath_attr ruleAttr = {};
    ruleAttr.allowed_access = effective;
    ruleAttr.parent_fd = fd;

    int ret = static_cast<int>(syscall(__NR_landlock_add_rule, rulesetFd,
                                       LANDLOCK_RULE_PATH_BENEATH, &ruleAttr, 0));
    close(fd);
    return ret >= 0;
}

bool addRule(int rulesetFd, const std::string &path, __u64 accessRights, __u64 handledAccess)
{
    // Mask to only handled rights
    const __u64 effectiveAccess = accessRights & handledAccess;
    if (effectiveAccess == 0)
        return true;

    int fd = open(path.c_str(), O_PATH | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            // Path doesn't exist — skip silently. It's inaccessible anyway.
            return true;
        }
        qWarning() << "Landlock: failed to open" << path.c_str() << ":" << strerror(errno);
        return false;
    }

    struct landlock_path_beneath_attr ruleAttr = {};
    ruleAttr.allowed_access = effectiveAccess;
    ruleAttr.parent_fd = fd;

    int ret = landlockAddRule(rulesetFd, LANDLOCK_RULE_PATH_BENEATH, &ruleAttr, 0);
    int savedErrno = errno;
    close(fd);

    if (ret < 0) {
        qWarning() << "Landlock: failed to add rule for" << path.c_str() << ":" << strerror(savedErrno);
        return false;
    }

    return true;
}

bool applyRuleset(int abi, const std::vector<PathRule> &rules)
{
    const __u64 handledAccess = handledAccessForAbi(abi);

    struct landlock_ruleset_attr attr = {};
    attr.handled_access_fs = handledAccess;

    int rulesetFd = landlockCreateRuleset(&attr, sizeof(attr), 0);
    if (rulesetFd < 0) {
        qWarning() << "Landlock: landlock_create_ruleset failed:" << strerror(errno);
        return false;
    }

    bool allOk = true;
    for (const PathRule &rule : rules) {
        if (!addRule(rulesetFd, rule.path, rule.access, handledAccess))
            allOk = false;
    }

    if (!allOk) {
        close(rulesetFd);
        qWarning() << "Landlock: some rules failed to add, aborting restriction";
        return false;
    }

    // prctl(PR_SET_NO_NEW_PRIVS) is required before landlock_restrict_self.
    // Flatpak already sets this, but calling it again is safe and idempotent.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        close(rulesetFd);
        qWarning() << "Landlock: prctl(PR_SET_NO_NEW_PRIVS) failed:" << strerror(errno);
        return false;
    }

    if (landlockRestrictSelf(rulesetFd, 0) < 0) {
        close(rulesetFd);
        qWarning() << "Landlock: landlock_restrict_self failed:" << strerror(errno);
        return false;
    }

    close(rulesetFd);
    return true;
}

// generated from permissions.yml.
std::vector<PathRule> browserConfigRules()
{
    const std::string home = homePath();
    const std::string xdgConfig = xdgConfigHome();
    std::vector<PathRule> result;
    result.reserve(Permissions::kUniqueConfigDirs.size());
    for (const Permissions::ConfigDirEntry &cd : Permissions::kUniqueConfigDirs) {
        const std::string &base =
            (cd.configLayout == Permissions::ConfigLayout::HomeRelative) ? home : xdgConfig;
        result.push_back({base + "/" + std::string(cd.configDir), kReadWriteCreate});
    }
    return result;
}

auto browserVarAppPaths()
{
    return Permissions::kBrowsers
        | std::views::transform([home = homePath()](const Permissions::BrowserEntry &b) {
            return home + "/.var/app/" + std::string(b.flatpakId);
        });
}

std::vector<PathRule> systemRules()
{
    const std::string home = homePath();
    const std::string appId = APP_ID;

    std::vector<PathRule> rules = {
        // App binaries, JRE, libs, extra-data
        {"/app",        kReadExec | kReadWrite},
        // Runtime (Qt, KDE, glibc, etc.)
        {"/usr",        kReadExec},
        // System/runtime config
        {"/etc",        kReadOnly},
        // Wayland, X11, D-Bus, pcscd sockets
        {"/run",        kReadWriteCreate},
        // X11 sockets, temp files
        {"/tmp",        kReadWriteCreate},
        // Process info (Java needs /proc/self)
        {"/proc",       kReadWrite},
        // DRI, urandom, null, etc.
        {"/dev",        kReadWriteCreate},
        // Required by JVM and Qt
        {"/sys",        kReadOnly},
        // Host flatpak icon dirs
        {"/var/lib/flatpak/exports/share/icons",   kReadOnly},
        {"/var/lib/flatpak/app",                   kReadOnly},
        // User flatpak icon dirs
        {home + "/.local/share/flatpak/exports/share/icons", kReadOnly},
        {home + "/.local/share/flatpak/app",                 kReadOnly},
        // App's XDG dirs (config, data, cache)
        {home + "/.var/app/" + appId,              kReadWriteCreate},
        // Java temp (via --persist)
        {home + "/.java",                          kReadWriteCreate},
        // Allow QSaveFile to create a temp file in $HOME and atomically rename
        // it to external_providers.xml (MAKE_REG + REMOVE_FILE needed on the dir)
        {home,                                     kOverridesDirOps},
        {home + "/external_providers.xml",         kOverrideFileAccess},
    };

    return rules;
}

// Overrides directory + specific override file rules
void addOverrideRules(std::vector<PathRule> &rules, const std::vector<std::string> &overrideFiles)
{
    const std::string home = homePath();
    const std::string overridesDir = home + "/.local/share/flatpak/overrides";

    // Directory-level: allow KConfig temp file operations + inotify
    rules.push_back({overridesDir, kOverridesDirOps});

    // Per-file read+write access for listed files
    for (const std::string &file : overrideFiles) {
        rules.push_back({overridesDir + "/" + file, kOverrideFileAccess});
    }
}

} // anonymous namespace

namespace LandlockSandbox {

int abiVersion()
{
    struct landlock_ruleset_attr attr = {};
    attr.handled_access_fs = 0;

    int abi = landlockCreateRuleset(nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 0) {
        if (errno == ENOSYS || errno == EOPNOTSUPP)
            return 0;
        return 0;
    }
    return abi;
}

bool limitOverrides()
{
    const int abi = abiVersion();
    if (abi <= 0) {
        qCritical() << "Landlock: not available on this kernel, refusing to continue";
        return false;
    }
    qInfo() << "Landlock: applying Phase 1 (limitOverrides), ABI version" << abi;

    // Start with system-level rules
    std::vector<PathRule> rules = systemRules();

    // Browser config directories (needed for NativeHostIntegrator)
    for (const PathRule &rule : browserConfigRules()) {
        rules.push_back(rule);
    }

    // Browser .var/app directories (Flatpak browser data)
    for (const std::string &path : browserVarAppPaths()) {
        rules.push_back({path, kReadWriteCreate});
    }

    // Override files: browsers + szafir + szafirhost (if unbundled)
    std::vector<std::string> overrideFiles;
    for (const Permissions::BrowserEntry &b : Permissions::kBrowsers) {
        overrideFiles.emplace_back(b.flatpakId);
    }
    overrideFiles.emplace_back("pl.kir.szafir");
#ifndef BUNDLED_HOST
    overrideFiles.emplace_back("pl.kir.szafirhost");
#endif
    addOverrideRules(rules, overrideFiles);

    if (!applyRuleset(abi, rules)) {
        qWarning() << "Landlock: Phase 1 restriction failed";
        return false;
    }

    qInfo() << "Landlock: Phase 1 (limitOverrides) applied successfully";
    return true;
}

bool dropBrowserAccess()
{
    const int abi = abiVersion();
    if (abi <= 0) {
        qCritical() << "Landlock: not available on this kernel, refusing to continue";
        return false;
    }
    qInfo() << "Landlock: applying Phase 2 (dropBrowserAccess), ABI version" << abi;

    // System rules remain the same — stacking means effective = intersection
    std::vector<PathRule> rules = systemRules();

    // Browser config dirs are NOT included — they get dropped by intersection.
    // Browser .var/app dirs are NOT included — dropped too.

    // Only szafir override files remain
    std::vector<std::string> overrideFiles;
    overrideFiles.emplace_back("pl.kir.szafir");
#ifndef BUNDLED_HOST
    overrideFiles.emplace_back("pl.kir.szafirhost");
#endif
    addOverrideRules(rules, overrideFiles);

    if (!applyRuleset(abi, rules)) {
        qWarning() << "Landlock: Phase 2 restriction failed";
        return false;
    }

    qInfo() << "Landlock: Phase 2 (dropBrowserAccess) applied successfully";
    return true;
}

// GCC 15+ no longer suppresses warn_unused_result via (void) cast in C++.
// write() failures in a signal-safe post-fork context cannot be handled, so
// silencing the warning here is intentional.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
void applyLauncherRestrictions(const char *home, const char *xdgDataHome, const char *xauthority)
{
    // Applies strict Landlock restrictions for the SzafirHost runtime child.
    // Async-signal-safe: uses only syscall, open, close, prctl, write, snprintf.
    // home and xdgDataHome are pre-captured in the parent process before fork.

    int abi = static_cast<int>(syscall(__NR_landlock_create_ruleset, nullptr, 0,
                                       LANDLOCK_CREATE_RULESET_VERSION));
    if (abi < 0) {
        const char msg[] = "LandlockLauncher: not available, aborting child\n";
        (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(1);
    }

    const __u64 handled = handledAccessForAbi(abi);

    struct landlock_ruleset_attr attr = {};
    attr.handled_access_fs = handled;

    int rulesetFd = static_cast<int>(syscall(__NR_landlock_create_ruleset,
                                             &attr, sizeof(attr), 0));
    if (rulesetFd < 0) {
        const char msg[] = "LandlockLauncher: create_ruleset failed, aborting child\n";
        (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(1);
    }

    bool allOk = true;
    auto applyRule = [rulesetFd, handled, &allOk](const char *path, __u64 access) {
        if (!addRuleRaw(rulesetFd, path, access, handled)) {
            const char prefix[] = "LandlockLauncher: rule failed for: ";
            (void)::write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
            (void)::write(STDERR_FILENO, path, strlen(path));
            (void)::write(STDERR_FILENO, "\n", 1);
            allOk = false;
        }
    };

    for (const Permissions::LauncherStaticRule &rule : Permissions::kLauncherStaticRules)
        applyRule(rule.path, rule.access);

    Permissions::forEachLauncherDynamicRule(home, xdgDataHome, xauthority, applyRule);

    if (!allOk) {
        const char msg[] = "LandlockLauncher: some rules failed, aborting child\n";
        (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        close(rulesetFd);
        _exit(1);
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        const char msg[] = "LandlockLauncher: prctl(PR_SET_NO_NEW_PRIVS) failed, aborting child\n";
        (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        close(rulesetFd);
        _exit(1);
    }

    if (syscall(__NR_landlock_restrict_self, rulesetFd, 0) < 0) {
        const char msg[] = "LandlockLauncher: restrict_self failed, aborting child\n";
        (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        close(rulesetFd);
        _exit(1);
    }

    close(rulesetFd);
}
#pragma GCC diagnostic pop

} // namespace LandlockSandbox
