#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\."
    r"(0|[1-9]\d*)\."
    r"(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9A-Za-z-][0-9A-Za-z-]*))*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def shell_bool(value: bool) -> str:
    return "true" if value else "false"


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate stackimport release version metadata.")
    parser.add_argument("--version-file", default="VERSION.txt", type=Path)
    parser.add_argument("--version", help="Requested SemVer version, without leading v.")
    parser.add_argument("--tag", help="Release tag, with leading v.")
    parser.add_argument("--github-output", type=Path, help="Append outputs for GitHub Actions.")
    args = parser.parse_args()

    source_version = args.version_file.read_text(encoding="utf-8").strip()
    match = SEMVER_RE.fullmatch(source_version)
    if not match:
        fail(f"{args.version_file} does not contain a valid SemVer 2.0.0 version: {source_version!r}")

    requested_version = args.version or source_version
    if requested_version != source_version:
        fail(f"requested version {requested_version!r} does not match {args.version_file}: {source_version!r}")

    tag = args.tag or f"v{source_version}"
    if tag != f"v{source_version}":
        fail(f"release tag {tag!r} must be v{source_version}")

    cmake_version = ".".join(match.group(index) for index in (1, 2, 3))
    prerelease = match.group(4) is not None

    outputs = {
        "version": source_version,
        "cmake_version": cmake_version,
        "tag": tag,
        "prerelease": shell_bool(prerelease),
    }

    for key, value in outputs.items():
        print(f"{key}={value}")

    if args.github_output:
        with args.github_output.open("a", encoding="utf-8") as handle:
            for key, value in outputs.items():
                handle.write(f"{key}={value}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
