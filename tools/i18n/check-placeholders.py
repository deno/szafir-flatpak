#!/usr/bin/env python3
"""Validate KI18n placeholders (%1, %2, ...) between msgid(s) and msgstr(s).

Policy:
  * placeholders are compared as multisets (repetition matters);
  * reordering is allowed; missing or invented placeholders fail;
  * escaped "%%" is ignored;
  * source msgid (and its plural form, if any) must use a contiguous %1..%N set;
  * plural forms: the English singular/plural msgids must share one multiset, and
    then every translated plural form must match it;
  * fuzzy and untranslated entries are skipped (the coverage check owns them);
  * obsolete entries are skipped.
"""
from __future__ import annotations

import re
import sys
from collections import Counter
from typing import Any

try:
    import polib
except ImportError as exc:  # pragma: no cover
    sys.exit(f"error: polib is required (pip install polib): {exc}")

PH_RE = re.compile(r"%(\d+)")


def placeholder_multiset(text: str) -> Counter:
    """Count %N placeholders, ignoring escaped literal percent signs (%%)."""
    return Counter(int(m.group(1)) for m in PH_RE.finditer(text.replace("%%", "")))


def is_contiguous(nums) -> bool:
    vals = sorted(int(n) for n in nums)
    return vals == list(range(1, len(vals) + 1))


def check_entry(entry: Any, path: str) -> list[str]:
    if entry.obsolete or entry.fuzzy:
        return []
    if not (entry.msgstr or entry.msgstr_plural):
        return []  # untranslated; owned by the coverage check

    errors: list[str] = []

    source_nums = sorted(placeholder_multiset(entry.msgid or ""))
    if source_nums and not is_contiguous(source_nums):
        errors.append(
            f"{path}: placeholders must start at %1 without gaps, got {source_nums} "
            f"for msgid {entry.msgid!r}"
        )

    if entry.msgid_plural:
        if placeholder_multiset(entry.msgid) != placeholder_multiset(entry.msgid_plural):
            errors.append(
                f"{path}: English singular/plural placeholder sets differ: "
                f"{entry.msgid!r} vs {entry.msgid_plural!r}"
            )
        expected = placeholder_multiset(entry.msgid)
        forms = {str(k): v for k, v in entry.msgstr_plural.items()}
    else:
        expected = placeholder_multiset(entry.msgid)
        forms = {"0": entry.msgstr}

    for form, text in forms.items():
        if not text:
            continue
        actual = placeholder_multiset(text)
        if actual != expected:
            errors.append(
                f"{path}: placeholder mismatch in plural[{form}] for {entry.msgid!r}: "
                f"expected {dict(expected)}, got {dict(actual)}"
            )
    return errors


def main(argv: list[str]) -> int:
    files = argv[1:] or ["szafir-host-proxy/translations/pl/szafir-host-proxy.po"]
    failed = 0
    for po_file in files:
        po = polib.pofile(po_file)
        for entry in po:
            for err in check_entry(entry, po_file):
                print(err, file=sys.stderr)
                failed = 1
    return failed


if __name__ == "__main__":
    sys.exit(main(sys.argv))
