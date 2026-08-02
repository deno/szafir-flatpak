# Translations

The SzafirHost Proxy UI is localized with GNU gettext catalogs consumed by
[KI18n](https://develop.kde.org/docs/frameworks/ki18n/). English is the source
language; each locale lives under `translations/<lang>/<domain>.po`.

## Layout

```
szafir-host-proxy/
├── Messages.sh          # KDE-conventional extraction entry point
└── translations/
    └── pl/
        └── szafir-host-proxy.po
tools/i18n/
├── update.sh            # refresh catalogs, or --check for drift (CI)
├── check-placeholders.py# KI18n %1/%2… substitution validation (uses polib)
└── requirements.txt     # polib (dev/CI only)
```

The translation domain (`szafir-host-proxy`) is defined once in
`szafir-host-proxy/CMakeLists.txt` (`TRANSLATION_DOMAIN`) and flows through
`config.h.in` into `KLocalizedString::setApplicationDomain()` in `main.cpp`.

## POT policy

The `.pot` template is **not committed**. It is regenerated deterministically on
demand by `Messages.sh` (and by `tools/i18n/update.sh` into a temporary
directory). This avoids timestamp-only diffs and the possibility that a committed
template and the committed catalogs drift apart. Reference locations are
file-only (`#: path/foo.qml`), not line-based, to keep diffs small.

## Refreshing a catalog from source

```bash
./tools/i18n/update.sh            # in-place refresh (review the diff, then commit)
./tools/i18n/update.sh --check    # drift check (exit non-zero if stale; CI-only, no writes)
```

`update.sh` runs `msgmerge --previous` (old msgids are kept on fuzzy entries for
review) followed by `msgattrib --no-obsolete`. Run it, review the diff, and
translate anything new/fuzzy.

## Quality policy

Three distinct conditions are enforced (see `.github/workflows/i18n.yml`):

| Condition | Meaning | CI action |
| --- | --- | --- |
| Invalid | malformed PO syntax, bad header, wrong format strings, bad placeholders | **fail** |
| Stale | source and catalog message sets differ (new/removed/changed strings) | **fail** (`update.sh --check`) |
| Incomplete | valid entries that are untranslated or fuzzy | **fail** for `pl` |

Statistics (translated/untranslated/fuzzy counts) are published to the job
summary without failing; the strict fail-on-incomplete policy for Polish is the
baseline because Polish is the only advertised UI translation.

## Adding a locale

1. Create `translations/<lang>/szafir-host-proxy.po` (copy the template from a
   dry run of `./tools/i18n/update.sh`).
2. Translate it fully (zero fuzzy, zero untranslated).
3. Add `translations/<lang>/szafir-host-proxy.po` to the CMake
   `translations` layout so `ki18n_install` picks it up, and extend the
   CI globs (`szafir-host-proxy/translations/*/*.po`).

## Translating source strings

See `CONTRIBUTING.md` → *i18n authoring rules*.
