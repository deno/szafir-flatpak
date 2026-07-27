#!/usr/bin/env python3
import argparse
import yaml
import sys
import os


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


def main():
    parser = argparse.ArgumentParser(description='Query release information')
    parser.add_argument('--current-version', action='store_true', help='Get the current version')
    parser.add_argument('--current-changelog', action='store_true',
                        help='Get an RPM %%changelog entry for the current version')
    args = parser.parse_args()

    if args.current_version:
        print(load_releases()[0]['version'])
    elif args.current_changelog:
        description = load_releases()[0]['description']['en']
        print(f"- {description}")
    else:
        parser.print_help()


if __name__ == '__main__':
    main()
