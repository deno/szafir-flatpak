#!/usr/bin/env python3
import argparse
import yaml
import sys
import os

FLATPAK_INSTALL_COMMAND = "flatpak install szafir pl.deno.kir.szafirhostproxy"


def load_releases():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    releases_file = os.path.join(script_dir, '..', 'szafir-host-proxy', 'releases.yml')

    if not os.path.isfile(releases_file):
        print(f"Missing releases file: {releases_file}", file=sys.stderr)
        sys.exit(1)

    with open(releases_file, 'r') as f:
        data = yaml.safe_load(f)

    if 'releases' not in data or not data['releases']:
        print("No releases found", file=sys.stderr)
        sys.exit(1)

    return data['releases']


def find_release(releases, version):
    for release in releases:
        if release['version'] == version:
            return release
    print(f"No entry for version {version} in szafir-host-proxy/releases.yml", file=sys.stderr)
    sys.exit(1)


def render_notes(release):
    """Render the releases.yml entry as GitHub release notes (Markdown).

    English first, then Polish: GitHub has no per-language release bodies, so both
    languages share one body. The text is the same one that feeds the AppStream
    metainfo and the RPM changelog.
    """
    description = release['description']
    lines = ["### What's changed", "", description['en'].strip()]

    if description.get('pl'):
        lines += ["", "### Zmiany", "", description['pl'].strip()]

    lines += [
        "",
        "### Install",
        "",
        "```sh",
        FLATPAK_INSTALL_COMMAND,
        "```",
        "",
        "The Flatpak bundle and the source archive are attached below.",
        "",
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description='Query release information')
    parser.add_argument('--current-version', action='store_true', help='Get the current version')
    parser.add_argument('--current-changelog', action='store_true',
                        help='Get an RPM %%changelog entry for the current version')
    parser.add_argument('--notes', metavar='VERSION',
                        help='Get GitHub release notes (Markdown) for VERSION')
    args = parser.parse_args()

    if args.current_version:
        print(load_releases()[0]['version'])
    elif args.current_changelog:
        description = load_releases()[0]['description']['en']
        print(f"- {description}")
    elif args.notes:
        version = args.notes.removeprefix('v')
        print(render_notes(find_release(load_releases(), version)))
    else:
        parser.print_help()


if __name__ == '__main__':
    main()
