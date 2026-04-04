#!/usr/bin/env python3
import argparse
import yaml
import sys
import os

def main():
    parser = argparse.ArgumentParser(description='Query release information')
    parser.add_argument('--current-version', action='store_true', help='Get the current version')
    args = parser.parse_args()

    if args.current_version:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        releases_file = os.path.join(script_dir, '..', 'szafir-host-proxy', 'releases.yml')

        if not os.path.isfile(releases_file):
            print(f"Missing releases file: {releases_file}", file=sys.stderr)
            sys.exit(1)

        with open(releases_file, 'r') as f:
            data = yaml.safe_load(f)

        if 'releases' in data and data['releases']:
            version = data['releases'][0]['version']
            print(version)
        else:
            print("No releases found", file=sys.stderr)
            sys.exit(1)
    else:
        parser.print_help()

if __name__ == '__main__':
    main()