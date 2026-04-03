#include "LandlockSandbox.h"
#include "config.h"

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

// ---- Access right sets for different ABI versions --------------------------

// ABI v1 (kernel 5.13)
constexpr __u64 kFsAccessV1 =
    LANDLOCK_ACCESS_FS_EXECUTE |
    LANDLOCK_ACCESS_FS_WRITE_FILE |
    LANDLOCK_ACCESS_FS_READ_FILE |
    LANDLOCK_ACCESS_FS_READ_DIR |
    LANDLOCK_ACCESS_FS_REMOVE_DIR |
    LANDLOCK_ACCESS_FS_REMOVE_FILE |
    LANDLOCK_ACCESS_FS_MAKE_CHAR |
    LANDLOCK_ACCESS_FS_MAKE_DIR |
    LANDLOCK_ACCESS_FS_MAKE_REG |
    LANDLOCK_ACCESS_FS_MAKE_SOCK |
    LANDLOCK_ACCESS_FS_MAKE_FIFO |
    LANDLOCK_ACCESS_FS_MAKE_BLOCK |
    LANDLOCK_ACCESS_FS_MAKE_SYM;

// ABI v2 adds LANDLOCK_ACCESS_FS_REFER
#ifdef LANDLOCK_ACCESS_FS_REFER
constexpr __u64 kFsAccessV2 = kFsAccessV1 | LANDLOCK_ACCESS_FS_REFER;
#else
constexpr __u64 kFsAccessV2 = kFsAccessV1;
#endif

// ABI v3 adds LANDLOCK_ACCESS_FS_TRUNCATE
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
constexpr __u64 kFsAccessV3 = kFsAccessV2 | LANDLOCK_ACCESS_FS_TRUNCATE;
#else
constexpr __u64 kFsAccessV3 = kFsAccessV2;
#endif

// ABI v5 adds LANDLOCK_ACCESS_FS_IOCTL_DEV
#ifdef LANDLOCK_ACCESS_FS_IOCTL_DEV
constexpr __u64 kFsAccessV5 = kFsAccessV3 | LANDLOCK_ACCESS_FS_IOCTL_DEV;
#else
constexpr __u64 kFsAccessV5 = kFsAccessV3;
#endif

__u64 handledAccessForAbi(int abi)
{
    if (abi >= 5) return kFsAccessV5;
    if (abi >= 3) return kFsAccessV3;
    if (abi >= 2) return kFsAccessV2;
    return kFsAccessV1;
}

// Common access right combinations
constexpr __u64 kReadExec =
    LANDLOCK_ACCESS_FS_EXECUTE |
    LANDLOCK_ACCESS_FS_READ_FILE |
    LANDLOCK_ACCESS_FS_READ_DIR;

constexpr __u64 kReadOnly =
    LANDLOCK_ACCESS_FS_READ_FILE |
    LANDLOCK_ACCESS_FS_READ_DIR;

constexpr __u64 kReadWrite =
    LANDLOCK_ACCESS_FS_READ_FILE |
    LANDLOCK_ACCESS_FS_READ_DIR |
    LANDLOCK_ACCESS_FS_WRITE_FILE |
    LANDLOCK_ACCESS_FS_REMOVE_FILE |
    LANDLOCK_ACCESS_FS_REMOVE_DIR |
    LANDLOCK_ACCESS_FS_MAKE_DIR |
    LANDLOCK_ACCESS_FS_MAKE_REG |
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
    LANDLOCK_ACCESS_FS_TRUNCATE |
#endif
    LANDLOCK_ACCESS_FS_MAKE_SYM;

constexpr __u64 kReadWriteCreate =
    kReadWrite |
    LANDLOCK_ACCESS_FS_MAKE_SOCK |
    LANDLOCK_ACCESS_FS_MAKE_FIFO;

// For the overrides directory: allow creating temp files (for KConfig and QSaveFile atomic writes),
// listing contents (for inotify), writing/removing files.
// Note: QSaveFile uses O_RDWR internally often, so READ_FILE and TRUNCATE are required.
constexpr __u64 kOverridesDirOps =
    LANDLOCK_ACCESS_FS_READ_DIR |
    LANDLOCK_ACCESS_FS_READ_FILE |
    LANDLOCK_ACCESS_FS_WRITE_FILE |
    LANDLOCK_ACCESS_FS_REMOVE_FILE |
    LANDLOCK_ACCESS_FS_MAKE_REG |
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
    LANDLOCK_ACCESS_FS_TRUNCATE |
#endif
    0;

// For individual override files: read + write
constexpr __u64 kOverrideFileAccess =
    LANDLOCK_ACCESS_FS_READ_FILE |
    LANDLOCK_ACCESS_FS_WRITE_FILE;

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

// Browser Flatpak IDs — kept in sync with NativeHostIntegrator.cpp browsers()
const std::vector<std::string> kBrowserFlatpakIds = {
    "org.mozilla.firefox",
    "io.gitlab.librewolf-community",
    "net.waterfox.waterfox",
    "com.google.Chrome",
    "com.google.ChromeDev",
    "org.chromium.Chromium",
    "io.github.ungoogled_software.ungoogled_chromium",
};

// Host browser config directories (relative to $HOME or $XDG_CONFIG_HOME)
struct BrowserConfigPath {
    std::string path;       // Full path
    bool isHomeRelative;    // true = under $HOME, false = under $XDG_CONFIG_HOME
};

std::vector<BrowserConfigPath> browserConfigPaths()
{
    const std::string home = homePath();
    const std::string xdgConfig = xdgConfigHome();

    return {
        {home + "/.mozilla",                       true},
        {home + "/.librewolf",                     true},
        {home + "/.waterfox",                      true},
        {xdgConfig + "/google-chrome",             false},
        {xdgConfig + "/google-chrome-unstable",    false},
        {xdgConfig + "/chromium",                  false},
    };
}

auto browserVarAppPaths()
{
    return kBrowserFlatpakIds
        | std::views::transform([home = homePath()](const std::string &id) {
            return home + "/.var/app/" + id;
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
        qWarning() << "Landlock: not available on this kernel, skipping Phase 1 restriction";
        return true;
    }
    qInfo() << "Landlock: applying Phase 1 (limitOverrides), ABI version" << abi;

    // Start with system-level rules
    std::vector<PathRule> rules = systemRules();

    // Browser config directories (needed for NativeHostIntegrator)
    for (const BrowserConfigPath &bcp : browserConfigPaths()) {
        rules.push_back({bcp.path, kReadWriteCreate});
    }

    // Browser .var/app directories (Flatpak browser data)
    for (const std::string &path : browserVarAppPaths()) {
        rules.push_back({path, kReadWriteCreate});
    }

    // Override files: browsers + szafir + szafirhost (if unbundled)
    std::vector<std::string> overrideFiles;
    for (const std::string &id : kBrowserFlatpakIds) {
        overrideFiles.push_back(id);
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
        qWarning() << "Landlock: not available on this kernel, skipping Phase 2 restriction";
        return true;
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
void applyLauncherRestrictions(const char *home, const char *xdgDataHome)
{
    // Applies strict Landlock restrictions for the SzafirHost runtime child.
    // Async-signal-safe: uses only syscall, open, close, prctl, write, snprintf.
    // home and xdgDataHome are pre-captured in the parent process before fork.

    int abi = static_cast<int>(syscall(__NR_landlock_create_ruleset, nullptr, 0,
                                       LANDLOCK_CREATE_RULESET_VERSION));
    if (abi < 0) {
        const char msg[] = "LandlockLauncher: not available, continuing unrestricted\n";
        (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return;
    }

    const __u64 handled = handledAccessForAbi(abi);

    struct landlock_ruleset_attr attr = {};
    attr.handled_access_fs = handled;

    int rulesetFd = static_cast<int>(syscall(__NR_landlock_create_ruleset,
                                             &attr, sizeof(attr), 0));
    if (rulesetFd < 0) {
        const char msg[] = "LandlockLauncher: create_ruleset failed\n";
        (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return;
    }

    char javaTmp[4096];
    snprintf(javaTmp, sizeof(javaTmp), "%s/.java", home);

    char extProviders[4096];
    snprintf(extProviders, sizeof(extProviders), "%s/external_providers.xml", home);

    char szafirInstallDir[4096];
    snprintf(szafirInstallDir, sizeof(szafirInstallDir), "%s/szafir_host", xdgDataHome);

    char verifiedComponentsDir[4096];
    snprintf(verifiedComponentsDir, sizeof(verifiedComponentsDir), "%s/szafir-host-proxy/components", xdgDataHome);

    struct { const char *path; __u64 access; } rules[] = {
        {"/app/jre",                               kReadExec},
        {"/app/bin/start-szafir-host-native.sh",   kReadExec},
        {"/usr",                                   kReadExec},
        {"/etc",                                   kReadOnly},
        {"/run/pcscd",                             kReadWriteCreate},
        {"/tmp",                                   kReadWriteCreate},
        {"/dev",                                   kReadWriteCreate},
        {"/proc",                                  kReadWrite},
        {"/sys",                                   kReadOnly},
        // external_providers.xml (read by SzafirHost)
        {extProviders,                             kReadOnly},
        // Dedicated verified component store (read-only for runtime).
        {verifiedComponentsDir,                    kReadOnly},
        // SzafirHost install dir and lock file parent area.
        {szafirInstallDir,                         kReadWriteCreate},
        {xdgDataHome,                              LANDLOCK_ACCESS_FS_READ_DIR},
        {javaTmp,                                  kReadWriteCreate},
    };

    bool allOk = true;
    for (const auto &rule : rules) {
        if (!addRuleRaw(rulesetFd, rule.path, rule.access, handled))
            allOk = false;
    }

    if (!allOk) {
        const char msg[] = "LandlockLauncher: some rules failed, continuing unrestricted\n";
        (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        close(rulesetFd);
        return;
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        close(rulesetFd);
        return;
    }

    if (syscall(__NR_landlock_restrict_self, rulesetFd, 0) < 0) {
        const char msg[] = "LandlockLauncher: restrict_self failed\n";
        (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        close(rulesetFd);
        return;
    }

    close(rulesetFd);
}
#pragma GCC diagnostic pop

} // namespace LandlockSandbox
