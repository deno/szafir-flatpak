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

import yaml
from jinja2 import Environment, FileSystemLoader, StrictUndefined

ROOT = Path(__file__).resolve().parents[1]


def _load_releases() -> list[dict[str, Any]]:
    data = yaml.safe_load(
        (ROOT / "szafir-host-proxy" / "releases.yml").read_text(encoding="utf-8")
    )
    releases = data["releases"]
    if not isinstance(releases, list) or not releases:
        raise RuntimeError("missing releases in szafir-host-proxy/releases.yml")
    first_release = releases[0]
    if "version" not in first_release:
        raise RuntimeError("missing version in first release entry in releases.yml")
    return releases


RELEASES: list[dict[str, Any]] = _load_releases()
APP_VERSION: str = RELEASES[0]["version"]


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


def _load_system_components() -> list[dict[str, Any]]:
    data = json.loads(
        (ROOT / "szafir-host-proxy" / "system_components.json").read_text(encoding="utf-8")
    )
    return data["system_components"]


def _get_system_component(component_id: str) -> dict[str, Any]:
    for comp in SYSTEM_COMPONENTS:
        if comp.get("id") == component_id:
            return comp
    raise RuntimeError(f"component '{component_id}' not found in system_components.json")


SYSTEM_COMPONENTS: list[dict[str, Any]] = _load_system_components()


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
            "system_components": [],
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
            "system_components": [_get_system_component("pcsc-lite")],
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
            "system_components": [],
            "include_installer_extra": False,
            "include_library_extra": False,
            "installer_extra_data": _installer_extra_data_entry(),
            "library_extra_data": _library_extra_data_entries(),
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
            "system_components": [_get_system_component("pcsc-lite")],
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
            "system_components": [_get_system_component("pcsc-lite")],
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
            "system_components": [],
            "include_installer_extra": True,
            "include_library_extra": False,
            "installer_extra_data": _installer_extra_data_entry(),
            "library_extra_data": _library_extra_data_entries(),
        },
    },
    "szafirhost.manifest": {
        "output": "pl.kir.szafirhost.yml",
        "template_root": "manifests",
        "template": "szafirhost.yml.j2",
        "context": {
            "system_components": [_get_system_component("pcsc-lite")],
        },
    },
    "szafir.manifest": {
        "output": "pl.kir.szafir.yml",
        "template_root": "manifests",
        "template": "szafir.yml.j2",
        "context": {
            "system_components": [_get_system_component("pcsc-lite")],
        },
    },
    # Metainfo XML files
    "proxy-split.meta": {
        "output": "pl.deno.kir.szafirhostproxy-split.metainfo.xml",
        "template_root": "metainfo",
        "template": "proxy.metainfo.xml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "releases": RELEASES,
            "summary_en": "Browser bridge for Szafir website signing",
            "summary_pl": "Most przeglądarkowy dla podpisu Szafir na stronach WWW",
        },
    },
    "proxy-inprocess.meta": {
        "output": "pl.deno.kir.szafirhostproxy-inprocess.metainfo.xml",
        "template_root": "metainfo",
        "template": "proxy.metainfo.xml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "releases": RELEASES,
            "summary_en": "Browser bridge for Szafir website signing",
            "summary_pl": "Most przeglądarkowy dla podpisu Szafir na stronach WWW",
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
