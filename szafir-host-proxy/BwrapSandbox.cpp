#include "BwrapSandbox.h"
#include "config.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

// Resolves the current executable's real path. We cannot pass the literal
// "/proc/self/exe" to bwrap: inside the sandbox /proc is remounted and
// "self" is bwrap, so it would re-exec bwrap instead of the app.
std::string selfExePath()
{
    char buf[4096];
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return buf;
    }
    return "/proc/self/exe";
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

std::vector<std::string> buildArgs(const std::string &bwrap, int argc, char *argv[])
{
    std::vector<std::string> a;
    a.push_back(bwrap);

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

    // Minimal /dev (null, zero, urandom, random, tty, ptmx)
    a.push_back("--dev");
    a.push_back("/dev");
    bindTry(a, "--dev-bind", "/dev/dri", "/dev/dri");

    // Private procfs (PID namespace: only own PIDs visible)
    a.push_back("--proc");
    a.push_back("/proc");

    // Private /tmp with sticky bit
    a.push_back("--perms");
    a.push_back("1777");
    a.push_back("--tmpfs");
    a.push_back("/tmp");

    // X11 socket (source resolved against host before pivot)
    bindTry(a, "--bind", "/tmp/.X11-unix", "/tmp/.X11-unix");

    // Runtime directory: D-Bus session bus, Wayland socket
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

    // Mark as wrapped so the re-execed process skips re-exec
    a.push_back("--setenv");
    a.push_back("SZAFIR_BWRAPPED");
    a.push_back("1");

    // Target: re-exec the same binary with original arguments
    a.push_back("--");
    a.push_back(selfExePath());
    for (int i = 1; i < argc; ++i)
        a.push_back(argv[i]);

    return a;
}

} // anonymous namespace

namespace BwrapSandbox {

bool inBwrap()
{
    const char *v = getenv("SZAFIR_BWRAPPED");
    return v && strcmp(v, "1") == 0;
}

bool maybeReExec(int argc, char *argv[])
{
#if !SZAFIR_BWRAP_ENABLED
    return true;
#else
    if (inBwrap())
        return true;

    const char *noBwrap = getenv("SZAFIR_NO_BWRAP");
    if (noBwrap && strcmp(noBwrap, "1") == 0) {
        fprintf(stderr, "szafir-host-proxy: SZAFIR_NO_BWRAP=1 — running without bwrap sandbox\n");
        return true;
    }

    const std::string bwrap = locateBwrap();
    if (bwrap.empty()) {
        fprintf(stderr, "szafir-host-proxy: bwrap not found; cannot sandbox\n");
        return false;
    }

    std::vector<std::string> args = buildArgs(bwrap, argc, argv);

    std::vector<char *> execArgs;
    execArgs.reserve(args.size() + 1);
    for (auto &s : args)
        execArgs.push_back(s.data());
    execArgs.push_back(nullptr);

    execvp(execArgs[0], execArgs.data());

    fprintf(stderr, "szafir-host-proxy: execvp(%s) failed: %s\n",
            execArgs[0], strerror(errno));
    return false;
#endif
}

} // namespace BwrapSandbox
