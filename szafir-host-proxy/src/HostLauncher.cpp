#include "HostLauncher.h"
#include "LandlockEnv.h"
#include "LandlockSandbox.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <unistd.h>

namespace HostLauncher {

int run(int argc, char *argv[])
{
    // Locate the command that follows the "--" separator.
    int i = 2;
    while (i < argc && strcmp(argv[i], "--") != 0)
        ++i;
    if (i + 1 >= argc) {
        fprintf(stderr, "szafir-host-proxy --launch-host: missing command after '--'\n");
        return 2;
    }
    char **cmd = &argv[i + 1];

    if (LandlockEnv::isModuleEnabled("LANDLOCK_LAUNCHER")) {
        const char *home = getenv("HOME");
        const char *xdg = getenv("XDG_DATA_HOME");
        const char *xauth = getenv("XAUTHORITY");
        const std::string homeStr = home ? home : "/";
        const std::string xdgStr = xdg ? xdg : (homeStr + "/.local/share");
        const std::string xauthStr = xauth ? xauth : "";
        LandlockSandbox::applyLauncherRestrictions(
            homeStr.c_str(), xdgStr.c_str(), xauthStr.c_str());
    }

    execvp(cmd[0], cmd);

    fprintf(stderr, "szafir-host-proxy --launch-host: execvp(%s) failed: %s\n",
            cmd[0], strerror(errno));
    return 127;
}

} // namespace HostLauncher
