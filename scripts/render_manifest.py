#!/usr/bin/env python3
"""Render Flatpak manifests and metainfo XML files from Jinja2 templates.

Usage:
    render_manifest.py all                  Render all variants
    render_manifest.py clean                Remove all generated output files
    render_manifest.py list                 List all variants and their outputs
    render_manifest.py outputs              Print output file paths (one per line)
    render_manifest.py <variant> [...]      Render one or more specific variants
"""
import sys
import json
from pathlib import Path
from typing import Any

from jinja2 import Environment, FileSystemLoader, StrictUndefined

ROOT = Path(__file__).resolve().parents[1]
APP_VERSION = "0.2.0"
APP_RELEASE_DATE = "2026-04-01"


def _load_download_components() -> dict[str, dict[str, Any]]:
    data = json.loads((ROOT / "components.json").read_text(encoding="utf-8"))
    components: dict[str, dict[str, Any]] = {}
    for component in data.get("components", []):
        component_id = component.get("id")
        if isinstance(component_id, str) and component_id:
            components[component_id] = component
    return components


def _component_field(components: dict[str, dict[str, Any]], component_id: str, field: str) -> Any:
    if component_id not in components:
        raise RuntimeError(f"missing component '{component_id}' in components.json")
    component = components[component_id]
    if field not in component:
        raise RuntimeError(
            f"missing field '{field}' for component '{component_id}' in components.json"
        )
    return component[field]


DOWNLOAD_COMPONENTS = _load_download_components()


def _extra_data_entry(component: dict[str, Any]) -> dict[str, Any]:
    return {
        "filename": component["filename"],
        "url": component["url"],
        "sha256": component["sha256"],
        "size": component["size"],
    }


def _library_extra_data_entries() -> list[dict[str, Any]]:
    return [
        _extra_data_entry(component)
        for component in DOWNLOAD_COMPONENTS.values()
        if component.get("type") == "library"
    ]


def _installer_extra_data_entry() -> dict[str, Any]:
    return _extra_data_entry(DOWNLOAD_COMPONENTS["szafirhost-installer"])


VARIANTS: dict[str, dict[str, Any]] = {
    # Flatpak manifests
    "proxy-split.manifest": {
        "output": "pl.deno.kir.szafirhostproxy-split.yml",
        "template_root": "manifests",
        "template": "proxy.yml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "metainfo_file": "pl.deno.kir.szafirhostproxy-split.metainfo.xml",
            "app_version": APP_VERSION,
            "bundled_host": False,
            "include_installer_extra": False,
            "include_library_extra": False,
        },
    },
    "proxy-inprocess--extra-all.manifest": {
        "output": "pl.deno.kir.szafirhostproxy-inprocess--extra-all.yml",
        "template_root": "manifests",
        "template": "proxy.yml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "metainfo_file": "pl.deno.kir.szafirhostproxy-inprocess.metainfo.xml",
            "app_version": APP_VERSION,
            "bundled_host": True,
            "include_installer_extra": True,
            "include_library_extra": True,
            "installer_extra_data": _installer_extra_data_entry(),
            "library_extra_data": _library_extra_data_entries(),
        },
    },
    "proxy-inprocess--extra-empty_missing.manifest": {
        "output": "pl.deno.kir.szafirhostproxy-inprocess--extra-empty_missing.yml",
        "template_root": "manifests",
        "template": "proxy.yml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "metainfo_file": "pl.deno.kir.szafirhostproxy-inprocess.metainfo.xml",
            "app_version": APP_VERSION,
            "bundled_host": True,
            "include_installer_extra": False,
            "include_library_extra": False,
            "installer_extra_data": _installer_extra_data_entry(),
            "library_extra_data": _library_extra_data_entries(),
            "include_pcsc_module": False
        },
    },
    "proxy-inprocess--extra-empty.manifest": {
        "output": "pl.deno.kir.szafirhostproxy-inprocess--extra-empty.yml",
        "template_root": "manifests",
        "template": "proxy.yml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "metainfo_file": "pl.deno.kir.szafirhostproxy-inprocess.metainfo.xml",
            "app_version": APP_VERSION,
            "bundled_host": True,
            "include_installer_extra": False,
            "include_library_extra": False,
            "installer_extra_data": _installer_extra_data_entry(),
            "library_extra_data": _library_extra_data_entries(),
        },
    },
    "proxy-inprocess--extra-runtime.manifest": {
        "output": "pl.deno.kir.szafirhostproxy-inprocess--extra-runtime.yml",
        "template_root": "manifests",
        "template": "proxy.yml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "metainfo_file": "pl.deno.kir.szafirhostproxy-inprocess.metainfo.xml",
            "app_version": APP_VERSION,
            "bundled_host": True,
            "include_installer_extra": True,
            "include_library_extra": False,
            "installer_extra_data": _installer_extra_data_entry(),
            "library_extra_data": _library_extra_data_entries(),
        },
    },
    "proxy-inprocess--extra-runtime_missing.manifest": {
        "output": "pl.deno.kir.szafirhostproxy-inprocess--extra-runtime_missing.yml",
        "template_root": "manifests",
        "template": "proxy.yml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "metainfo_file": "pl.deno.kir.szafirhostproxy-inprocess.metainfo.xml",
            "app_version": APP_VERSION,
            "bundled_host": True,
            "include_installer_extra": True,
            "include_library_extra": False,
            "installer_extra_data": _installer_extra_data_entry(),
            "library_extra_data": _library_extra_data_entries(),
            "include_pcsc_module": False
        },
    },
    "szafirhost.manifest": {
        "output": "pl.kir.szafirhost.yml",
        "template_root": "manifests",
        "template": "szafirhost.yml.j2",
        "context": {
            "include_pcsc_module": True,
            "pcsc_lite": DOWNLOAD_COMPONENTS["pcsc-lite"],
        },
    },
    # Metainfo XML files
    "proxy-split.meta": {
        "output": "pl.deno.kir.szafirhostproxy-split.metainfo.xml",
        "template_root": "metainfo",
        "template": "proxy.metainfo.xml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "app_version": APP_VERSION,
            "app_release_date": APP_RELEASE_DATE,
            "summary_en": "Proxy for Szafir browser integration",
            "summary_pl": "Proxy integracji przeglądarkowej dla środowiska Szafir",
        },
    },
    "proxy-inprocess.meta": {
        "output": "pl.deno.kir.szafirhostproxy-inprocess.metainfo.xml",
        "template_root": "metainfo",
        "template": "proxy.metainfo.xml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "app_version": APP_VERSION,
            "app_release_date": APP_RELEASE_DATE,
            "summary_en": "Proxy for Szafir browser integration (Flathub version)",
            "summary_pl": "Proxy integracji przeglądarkowej dla środowiska Szafir (wersja Flathub)",
        },
    },
    "szafirhost.meta": {
        "output": "pl.kir.szafirhost.metainfo.xml",
        "template_root": "metainfo",
        "template": "szafirhost.metainfo.xml.j2",
        "context": {},
    },
}

_COMMANDS = {"all", "clean", "list", "outputs"}


def _make_env(template_root: str) -> Environment:
    return Environment(
        loader=FileSystemLoader(str(ROOT / "templates" / template_root)),
        undefined=StrictUndefined,
        trim_blocks=True,
        lstrip_blocks=True,
        keep_trailing_newline=True,
    )


def render_variant(name: str) -> None:
    spec = VARIANTS[name]
    env = _make_env(spec["template_root"])
    context = dict(spec["context"])
    if (context.get("bundled_host") or context.get("include_pcsc_module")) and "pcsc_lite" not in context:
        context["pcsc_lite"] = DOWNLOAD_COMPONENTS["pcsc-lite"]
    rendered = env.get_template(spec["template"]).render(**context)
    output_path = ROOT / spec["output"]
    output_path.write_text(rendered, encoding="utf-8")
    print(f"  rendered  {spec['output']}")


def cmd_all() -> None:
    for name in VARIANTS:
        render_variant(name)


def cmd_clean() -> None:
    for spec in VARIANTS.values():
        p = ROOT / spec["output"]
        if p.exists():
            p.unlink()
            print(f"  removed   {spec['output']}")


def cmd_list() -> None:
    max_name = max(len(n) for n in VARIANTS)
    for name, spec in VARIANTS.items():
        print(f"  {name:<{max_name}}  →  {spec['output']}")


def cmd_outputs() -> None:
    for spec in VARIANTS.values():
        print(spec["output"])


def main() -> None:
    targets = sys.argv[1:]
    if not targets:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    if targets == ["all"]:
        cmd_all()
    elif targets == ["clean"]:
        cmd_clean()
    elif targets == ["list"]:
        cmd_list()
    elif targets == ["outputs"]:
        cmd_outputs()
    else:
        unknown = [t for t in targets if t not in VARIANTS]
        if unknown:
            print(f"error: unknown target(s): {', '.join(unknown)}", file=sys.stderr)
            print("       run 'list' to see available variants", file=sys.stderr)
            sys.exit(1)
        for name in targets:
            render_variant(name)


if __name__ == "__main__":
    main()
