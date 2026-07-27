#include "BwrapSandbox.h"
#include "config.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>
#include <unistd.h>

namespace {

bool pathExists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

std::string locateBwrap()
{
    if (BWRAP_PATH[0] != '\0' && access(BWRAP_PATH, X_OK) == 0)
        return BWRAP_PATH;

    const char *pathEnv = getenv("PATH");
    if (!pathEnv)
        return {};

    std::string paths(pathEnv);
    size_t start = 0;
    while (start < paths.size()) {
        size_t end = paths.find(':', start);
        if (end == std::string::npos)
            end = paths.size();
        std::string candidate = paths.substr(start, end - start) + "/bwrap";
        if (access(candidate.c_str(), X_OK) == 0)
            return candidate;
        start = end + 1;
    }
    return {};
}

std::string runtimeDir()
{
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0')
        return xdg;
    return "/run/user/" + std::to_string(getuid());
}

// Appends a bind mount if the source path exists (for bwrap versions without -try).
void bindTry(std::vector<std::string> &args, const char *flag,
             const std::string &src, const std::string &dest)
{
    if (!pathExists(src.c_str()))
        return;
    args.push_back(flag);
    args.push_back(src);
    args.push_back(dest);
}

} // anonymous namespace

namespace BwrapSandbox {

bool childWrappingEnabled()
{
#if !SZAFIR_BWRAP_ENABLED
    return false;
#else
    const char *noBwrap = getenv("SZAFIR_NO_BWRAP");
    if (noBwrap && strcmp(noBwrap, "1") == 0)
        return false;
    return !locateBwrap().empty();
#endif
}

std::string bwrapPath()
{
    return locateBwrap();
}

std::string selfExePath()
{
    // Resolve the real executable path. We cannot pass the literal "/proc/self/exe"
    // to bwrap: inside the sandbox /proc is remounted and "self" would be bwrap, so
    // it would exec bwrap instead of the shim.
    char buf[4096];
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return buf;
    }
    return "/proc/self/exe";
}

std::vector<std::string> childSandboxArgs(const std::string &launcherExe,
                                          const std::string &cmd,
                                          const std::vector<std::string> &cmdArgs)
{
    std::vector<std::string> a;

    // Namespaces
    a.push_back("--unshare-pid");
    a.push_back("--unshare-uts");
    a.push_back("--unshare-ipc");
    a.push_back("--unshare-cgroup-try");

    // Process
    a.push_back("--die-with-parent");
    a.push_back("--new-session");

    // Empty root — only explicit binds are visible
    a.push_back("--tmpfs");
    a.push_back("/");

    // Nix store (libs, JRE, assets)
    if (pathExists("/nix/store")) {
        a.push_back("--ro-bind");
        a.push_back("/nix/store");
        a.push_back("/nix/store");
    }

    // Runtime prefix (app binary, scripts) — may differ from /nix/store on non-Nix
    const std::string prefix = RUNTIME_PREFIX;
    if (prefix != "/app" && prefix.rfind("/nix/store", 0) != 0 && pathExists(prefix.c_str())) {
        a.push_back("--ro-bind");
        a.push_back(prefix);
        a.push_back(prefix);
    }

    // System paths
    bindTry(a, "--ro-bind", "/usr", "/usr");
    a.push_back("--ro-bind");
    a.push_back("/etc");
    a.push_back("/etc");
    a.push_back("--ro-bind");
    a.push_back("/sys");
    a.push_back("/sys");

    // /bin (and the dynamic-linker dirs /lib, /lib64) so the start script's
    // #!/bin/sh shebang and the JRE's interpreter resolve on glibc distros where
    // these are symlinks into /usr. bwrap resolves the symlink source.
    bindTry(a, "--ro-bind", "/bin", "/bin");
    bindTry(a, "--ro-bind", "/lib", "/lib");
    bindTry(a, "--ro-bind", "/lib64", "/lib64");

    // Minimal /dev (null, zero, urandom, random, tty, ptmx).
    // /dev/dri is deliberately NOT bound: the Java runtime software-renders and
    // does not need GPU acceleration.
    a.push_back("--dev");
    a.push_back("/dev");

    // Private procfs (PID namespace: only own PIDs visible)
    a.push_back("--proc");
    a.push_back("/proc");

    // Private /tmp with sticky bit
    a.push_back("--perms");
    a.push_back("1777");
    a.push_back("--tmpfs");
    a.push_back("/tmp");

    // X11 socket (display)
    bindTry(a, "--bind", "/tmp/.X11-unix", "/tmp/.X11-unix");

    // Runtime directory: Wayland socket, D-Bus session bus
    const std::string rd = runtimeDir();
    if (pathExists(rd.c_str())) {
        a.push_back("--bind");
        a.push_back(rd);
        a.push_back(rd);
    }

    // Smartcard daemon socket
    bindTry(a, "--bind", "/run/pcscd", "/run/pcscd");

    // User home (Landlock restricts within)
    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
        a.push_back("--bind");
        a.push_back(home);
        a.push_back(home);
    }

    // Mark as wrapped so HostLauncher forces Landlock on inside the namespace
    a.push_back("--setenv");
    a.push_back("SZAFIR_BWRAPPED");
    a.push_back("1");

    // Target: the launch shim applies Landlock, then execs the host command
    a.push_back("--");
    a.push_back(launcherExe);
    a.push_back("--launch-host");
    a.push_back("--");
    a.push_back(cmd);
    for (const std::string &arg : cmdArgs)
        a.push_back(arg);

    return a;
}

} // namespace BwrapSandbox
