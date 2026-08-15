import argparse
import subprocess
import sys
from pathlib import Path

DIR = Path(__file__).parent


def get_git_version() -> str:
    result = subprocess.run(
        ["git", "describe", "--always", "--dirty=-dirty"],
        cwd=DIR,
        stdout=subprocess.PIPE,
        stderr=sys.stderr,  # If the command fails (particularly in CI) we want to know why
        text=True,
        check=True,
    )
    return result.stdout.strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=str, required=True)

    args = parser.parse_args()
    out = Path(args.out)

    version = get_git_version()
    new_text = f"""
// AUTO-GENERATED FILE, DO NOT EDIT
// See git_version.py for details
#pragma once

#define SUPERBLT_VERSION_MACRO "{version}"
""".strip() + "\n"

    # Some build backends create the parent folders (eg Ninja), others seem not to (eg VS)
    if not out.parent.exists():
        out.parent.mkdir(parents=True)

    # Grab the current version of the file. If it matches what we were about
    # to write, then leave it alone to avoid updating the last-modified timestamp.
    # This means we won't unnecessarily re-compile anything.
    current_text = None
    if out.exists():
        with open(out, 'r') as f:
            current_text = f.read()

    if current_text != new_text:
        with open(out, 'w') as f:
            f.write(new_text)


if __name__ == "__main__":
    main()
