#pragma once

namespace LandlockSandbox {

// Returns the highest supported Landlock ABI version, or 0 if unavailable.
int abiVersion();

// Phase 1: Restrict filesystem to only paths the application needs.
// In particular, limits ~/.local/share/flatpak/overrides to only
// browser and szafir-related override files.
// Returns true on success. Returns false if Landlock is unavailable or any
// rule or restriction step fails. The caller must abort the process on failure.
bool limitOverrides();

// Phase 2: Drop access to browser config directories and ~/.var/app/<browser>
// directories. After this call, only szafir/szafirhost override files and
// application-internal paths remain accessible.
// Returns true on success. Returns false if Landlock is unavailable or any
// rule or restriction step fails. The caller must abort the process on failure.
bool dropBrowserAccess();

// Apply strict Landlock restrictions for a SzafirHost child process.
// Call ONLY from QProcess::setChildProcessModifier (post-fork, pre-exec).
// home and xdgDataHome MUST be pre-captured in the parent before fork.
// Uses only async-signal-safe functions.
// Calls _exit(1) on any failure — the child is NEVER allowed to run unrestricted.
void applyLauncherRestrictions(const char *home, const char *xdgDataHome);

} // namespace LandlockSandbox
