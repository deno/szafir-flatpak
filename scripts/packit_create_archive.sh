#!/usr/bin/env bash
# Create the source archive packit feeds to the SRPM as Source0.
# Reuses the same CPack artifact the release workflow publishes (make dist),
# so the RPM is built from exactly what a release ships. Prints the archive
# name last; packit parses create-archive stdout for an existing file path.
set -euo pipefail

make dist >/dev/null
version=$(python3 scripts/query_releases.py --current-version)
archive="szafir-host-proxy-${version}-source.tar.gz"
cp "buildsrc/${archive}" "${archive}"
echo "${archive}"
