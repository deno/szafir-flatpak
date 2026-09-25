# Contributing

Thanks for contributing to the Szafir Flatpak repository.

## Releasing

`szafir-host-proxy/releases.yml` is the single source of truth for the proxy
version. Everything else — the Flatpak manifests, metainfo, RPM spec, and Nix
flake — is generated from it, so a release only touches that one file by hand:

1. Add a new entry to the top of `szafir-host-proxy/releases.yml` (version, date, en/pl description).
2. `make manifests` to regenerate the manifests, metainfo, `szafir-host-proxy.spec`, and `flake.nix` (the pre-commit hook does this too).
3. Commit (`release X.Y.Z`), tag `vX.Y.Z`, and push — the tag drives the release workflow.

## i18n authoring rules

The proxy UI uses [KI18n](https://develop.kde.org/docs/frameworks/ki18n/) for
localization (see `szafir-host-proxy/translations/README.md`). When touching
user-visible strings in C++ or QML, follow these rules — CI enforces the
consequences (catalog drift, untranslated/fuzzy entries, placeholder errors).

* **Every user-visible string goes through KI18n** — `i18n("…")` in C++ and QML.
  Never concatenate translated fragments to build a sentence; pass values as
  placeholders instead.
* **Use `i18nc()` when a short string is ambiguous** — the first argument is a
  translator context and is not shown to users:
  ```cpp
  i18nc("@action:button", "Download Components")
  ```
* **Use `i18np()` / `i18ncp()` for grammatical plurals** (Polish has three plural
  forms):
  ```cpp
  i18np("%1 host active", "%1 hosts active", count)
  ```
* **Placeholders:** KI18n placeholders begin at `%1` and are contiguous. A
  translation must use the exact same multiset of placeholders (reordering is
  fine, dropping or inventing placeholders is not). `%%` is a literal percent.
* **Add translator comments** when a placeholder's meaning is non-obvious:
  ```cpp
  // i18n: %1 is the downloaded component version.
  i18n("Update to %1 available", version)
  ```
* **Prefer KUIT semantic markup** over raw HTML where KUIT is available.
* **After adding/changing strings**, refresh the catalog and translate:
  ```bash
  ./tools/i18n/update.sh        # then translate new/fuzzy entries
  ./tools/i18n/update.sh --check
  ```
