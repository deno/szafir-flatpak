#pragma once

// Qt-free bubblewrap re-exec engine. Called before QApplication construction.

namespace BwrapSandbox {

bool inBwrap();

// Re-exec the current process under bwrap. Returns true when already wrapped,
// disabled, or running inside Flatpak. Returns false on fatal error (caller
// must abort). On success the process image is replaced and this never returns.
bool maybeReExec(int argc, char *argv[]);

} // namespace BwrapSandbox
