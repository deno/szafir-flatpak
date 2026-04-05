#pragma once

// Common Landlock filesystem access-right constants and ABI helpers.
// This header has no dependencies beyond <linux/landlock.h> and may be safely
// included from post-fork, async-signal-safe contexts (no Qt, no libc allocations).

#include <linux/landlock.h>

namespace Landlock {

// ── ABI bitmask sets ─────────────────────────────────────────────────────────

// ABI v1 (kernel 5.13)
inline constexpr __u64 kFsAccessV1 =
    LANDLOCK_ACCESS_FS_EXECUTE    |
    LANDLOCK_ACCESS_FS_WRITE_FILE |
    LANDLOCK_ACCESS_FS_READ_FILE  |
    LANDLOCK_ACCESS_FS_READ_DIR   |
    LANDLOCK_ACCESS_FS_REMOVE_DIR |
    LANDLOCK_ACCESS_FS_REMOVE_FILE|
    LANDLOCK_ACCESS_FS_MAKE_CHAR  |
    LANDLOCK_ACCESS_FS_MAKE_DIR   |
    LANDLOCK_ACCESS_FS_MAKE_REG   |
    LANDLOCK_ACCESS_FS_MAKE_SOCK  |
    LANDLOCK_ACCESS_FS_MAKE_FIFO  |
    LANDLOCK_ACCESS_FS_MAKE_BLOCK |
    LANDLOCK_ACCESS_FS_MAKE_SYM;

// ABI v2 adds LANDLOCK_ACCESS_FS_REFER
#ifdef LANDLOCK_ACCESS_FS_REFER
inline constexpr __u64 kFsAccessV2 = kFsAccessV1 | LANDLOCK_ACCESS_FS_REFER;
#else
inline constexpr __u64 kFsAccessV2 = kFsAccessV1;
#endif

// ABI v3 adds LANDLOCK_ACCESS_FS_TRUNCATE
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
inline constexpr __u64 kFsAccessV3 = kFsAccessV2 | LANDLOCK_ACCESS_FS_TRUNCATE;
#else
inline constexpr __u64 kFsAccessV3 = kFsAccessV2;
#endif

// ABI v5 adds LANDLOCK_ACCESS_FS_IOCTL_DEV
#ifdef LANDLOCK_ACCESS_FS_IOCTL_DEV
inline constexpr __u64 kFsAccessV5 = kFsAccessV3 | LANDLOCK_ACCESS_FS_IOCTL_DEV;
#else
inline constexpr __u64 kFsAccessV5 = kFsAccessV3;
#endif

/// Returns the bitmask of all access rights supported by the given ABI version.
inline __u64 handledAccessForAbi(int abi) noexcept
{
    if (abi >= 5) return kFsAccessV5;
    if (abi >= 3) return kFsAccessV3;
    if (abi >= 2) return kFsAccessV2;
    return kFsAccessV1;
}

// ── Semantic access right combinations ──────────────────────────────────────
//
// *File variants omit directory bits (READ_DIR, MAKE_*, REMOVE_*); use them
// when the target path is a known regular file — Landlock returns EINVAL if
// directory-only bits are set for a non-directory inode.

inline constexpr __u64 kReadExec =
    LANDLOCK_ACCESS_FS_EXECUTE  |
    LANDLOCK_ACCESS_FS_READ_FILE|
    LANDLOCK_ACCESS_FS_READ_DIR;

inline constexpr __u64 kReadExecFile =
    LANDLOCK_ACCESS_FS_EXECUTE  |
    LANDLOCK_ACCESS_FS_READ_FILE;

inline constexpr __u64 kReadOnly =
    LANDLOCK_ACCESS_FS_READ_FILE|
    LANDLOCK_ACCESS_FS_READ_DIR;

inline constexpr __u64 kReadOnlyFile =
    LANDLOCK_ACCESS_FS_READ_FILE;

inline constexpr __u64 kReadDirOnly =
    LANDLOCK_ACCESS_FS_READ_DIR;

inline constexpr __u64 kReadWrite =
    LANDLOCK_ACCESS_FS_READ_FILE  |
    LANDLOCK_ACCESS_FS_READ_DIR   |
    LANDLOCK_ACCESS_FS_WRITE_FILE |
    LANDLOCK_ACCESS_FS_REMOVE_FILE|
    LANDLOCK_ACCESS_FS_REMOVE_DIR |
    LANDLOCK_ACCESS_FS_MAKE_DIR   |
    LANDLOCK_ACCESS_FS_MAKE_REG   |
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
    LANDLOCK_ACCESS_FS_TRUNCATE   |
#endif
    LANDLOCK_ACCESS_FS_MAKE_SYM;

inline constexpr __u64 kReadWriteCreate =
    kReadWrite                    |
    LANDLOCK_ACCESS_FS_MAKE_SOCK  |
    LANDLOCK_ACCESS_FS_MAKE_FIFO  |
#ifdef LANDLOCK_ACCESS_FS_REFER
    LANDLOCK_ACCESS_FS_REFER      |
#endif
    0;

// Union of read+exec and read+write — used for /app which needs execute access
// (JRE, bundled binaries) and full write access (JRE extraction, extra-data).
inline constexpr __u64 kReadExecWrite =
    kReadExec | kReadWrite;

// For the overrides directory: allow creating temp files (for KConfig and QSaveFile atomic writes),
// listing contents (for inotify), writing/removing files.
// Note: QSaveFile uses O_RDWR internally often, so READ_FILE and TRUNCATE are required.
inline constexpr __u64 kOverridesDirOps =
    LANDLOCK_ACCESS_FS_READ_DIR   |
    LANDLOCK_ACCESS_FS_READ_FILE  |
    LANDLOCK_ACCESS_FS_WRITE_FILE |
    LANDLOCK_ACCESS_FS_REMOVE_FILE|
    LANDLOCK_ACCESS_FS_MAKE_REG   |
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
    LANDLOCK_ACCESS_FS_TRUNCATE   |
#endif
    0;

// For individual override files: read + write
inline constexpr __u64 kOverrideFileAccess =
    LANDLOCK_ACCESS_FS_READ_FILE  |
    LANDLOCK_ACCESS_FS_WRITE_FILE;

} // namespace Landlock
