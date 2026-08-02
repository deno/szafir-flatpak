#!/usr/bin/env bash
# i18n update / drift-check driver.
#
#   tools/i18n/update.sh          # refresh PO catalogs in place (for review)
#   tools/i18n/update.sh --check  # verify catalogs are in sync with sources (drift only; CI-safe)
#
# drIfT detection = structural comparison only (new/changed/removed source strings,
# stale references, omitted QML). Translation completeness (untranslated/fuzzy) is a
# separate coverage step in CI.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="$ROOT/szafir-host-proxy"
MESSAGES_SH="$APP/Messages.sh"
TRANSLATIONS="$APP/translations"

# Derive the translation domain from the catalogs; fail if locale dirs diverge.
mapfile -t DOMAINS < <(find "$TRANSLATIONS" -name '*.po' -printf '%f\n' | sed 's/\.po$//' | sort -u)
if [ "${#DOMAINS[@]}" -eq 0 ]; then
    echo "error: no .po catalogs under $TRANSLATIONS" >&2
    exit 1
fi
if [ "${#DOMAINS[@]}" -gt 1 ]; then
    echo "error: locale catalogs use different domains: ${DOMAINS[*]}" >&2
    exit 1
fi
DOMAIN="${DOMAINS[0]}"

check_mode=0
if [ "${1:-}" = "--check" ]; then
    check_mode=1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Extract current source strings into a temporary template (never committed).
OUTDIR="$TMP" "$MESSAGES_SH"
POT="$TMP/$DOMAIN.pot"

# Timestamps churn on every extraction; strip them so drift comparison and
# re-running update mode are byte-deterministic.
strip_header_timestamps() {
    sed -E '/^"(POT-Creation-Date|PO-Revision-Date):/d' "$1"
}

mapfile -t POS < <(find "$TRANSLATIONS" -name '*.po')
failed=0
for po in "${POS[@]}"; do
    lang="$(basename "$(dirname "$po")")"
    merged="$TMP/$lang.merged.po"
    final="$TMP/$lang.final.po"

    # msgmerge first (--previous keeps the old msgid so a human can review fuzzy
    # suggestions); obsolete entries are pruned separately with msgattrib.
    # References come from the POT, which already carries file-only locations.
    msgmerge --previous --output-file="$merged" "$po" "$POT"
    msgattrib --no-obsolete --output-file="$final" "$merged"

    if [ "$check_mode" -eq 1 ]; then
        if ! diff -q <(strip_header_timestamps "$final") <(strip_header_timestamps "$po") >/dev/null; then
            echo "STALE: $po" >&2
            failed=1
        else
            echo "ok: $po" >&2
        fi
    else
        cp "$final" "$po"
        # Keep the committed catalog free of regeneration timestamps.
        strip_header_timestamps "$po" > "$po.tmp" && mv "$po.tmp" "$po"
        echo "updated $po" >&2
    fi
done

exit "$failed"
