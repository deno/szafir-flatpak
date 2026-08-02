#!/usr/bin/env bash
# KDE-conventional i18n extraction entry point for szafir-host-proxy.
#
# Extracts translatable UI strings from the C++ and QML sources into a single
# gettext template:
#   <OUTDIR>/szafir-host-proxy.pot
#
# OUTDIR defaults to <app>/translations/; tools/i18n/update.sh sets OUTDIR to a
# temporary directory so the template is never committed.
set -euo pipefail

SRCDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTDIR="${OUTDIR:-$SRCDIR/translations}"
mkdir -p "$OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"
POT="$OUTDIR/szafir-host-proxy.pot"

XGETTEXT_OPTIONS=(
    -LC++
    --from-code=UTF-8
    --add-comments=i18n:
    --keyword=i18n:1
    --keyword=i18nc:1c,2
    --keyword=i18np:1,2
    --keyword=i18ncp:1c,2,3
    --keyword=tr:1
    --keyword=tr:2c,1
)

# Deterministic relative file list: sort the inputs so output order is stable
# and the generated references do not contain the checkout's absolute path.
mapfile -t SOURCES < <(cd "$SRCDIR" && find src qml \
    \( -name '*.cpp' -o -name '*.h' -o -name '*.qml' \) | sort)

xgettext -D "$SRCDIR" "${XGETTEXT_OPTIONS[@]}" -o "$POT" -- "${SOURCES[@]}"

# Normalize every 'file:line' reference to file-only ('#: path/foo.qml'), so
# moving lines around does not produce noisy PO diffs.
sed -i -E '/^#[[:space:]]*:/ s@:[0-9]+([[:space:]]|$)@\1@g' "$POT"

# Drop POT-Creation-Date so regenerating the template never changes the header
# (keeps the committed catalog byte-stable across runs).
sed -i -E '/^"(POT-Creation-Date):/d' "$POT"

echo "Wrote $POT ($(grep -c '^msgid ' "$POT" || true) messages)" >&2
