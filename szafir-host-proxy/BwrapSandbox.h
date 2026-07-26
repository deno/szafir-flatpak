#pragma once

#include <string>
#include <vector>

// Qt-free bubblewrap wrapping for spawned SzafirHost runtime children.
//
// The proxy GUI itself runs natively (full GPU/display); only the runtime child
// is placed in a bwrap namespace. Landlock is applied *inside* that namespace by
// the HostLauncher shim, so it must come after bwrap's mount setup.

namespace BwrapSandbox {

// True when spawned host children should be wrapped in bwrap: enabled at build
// time, not disabled via SZAFIR_NO_BWRAP, and a bwrap binary can be located.
bool childWrappingEnabled();

// Absolute path to the bwrap binary, or empty if none was found.
std::string bwrapPath();

// Real path of the current executable (the proxy binary). Used as the
// --launch-host shim that bwrap execs inside the child namespace.
std::string selfExePath();

// Build the bwrap argument vector (excluding the program name) that wraps a host
// command in its own namespace:
//   --unshare-* / binds ... -- <launcherExe> --launch-host -- <cmd> <cmdArgs...>
// The child gets display access (X11 socket, Wayland/XDG_RUNTIME_DIR) but no GPU
// (/dev/dri is deliberately not bound).
std::vector<std::string> childSandboxArgs(const std::string &launcherExe,
                                          const std::string &cmd,
                                          const std::vector<std::string> &cmdArgs);

} // namespace BwrapSandbox
