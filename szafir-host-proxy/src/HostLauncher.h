#pragma once

namespace HostLauncher {

// Launcher shim mode, executed *inside* a bwrap namespace. Applies the Landlock
// launcher restrictions and then exec()s the requested command.
//
// argv layout:  proxy --launch-host -- <command> [args...]
//
// Landlock must be applied here (after bwrap has built the mount namespace) rather
// than in the parent's fork handler: applying it before bwrap runs would deny bwrap's
// own mount-point setup. Inside bwrap SZAFIR_BWRAPPED=1 forces Landlock on, so the
// child can never run unrestricted.
//
// On success this never returns (the process image is replaced). It returns only on
// failure: 2 for malformed arguments, 127 if exec fails.
int run(int argc, char *argv[]);

} // namespace HostLauncher
