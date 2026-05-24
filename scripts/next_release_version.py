#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\."
    r"(0|[1-9]\d*)\."
    r"(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9A-Za-z-][0-9A-Za-z-]*))*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def read_version(path: Path) -> tuple[int, int, int]:
    version = path.read_text(encoding="utf-8").strip()
    match = SEMVER_RE.fullmatch(version)
    if not match:
        fail(f"{path} does not contain a valid SemVer 2.0.0 version: {version!r}")
    if match.group(4) or match.group(5):
        fail("automatic patch bumps require VERSION.txt to contain a stable MAJOR.MINOR.PATCH version")
    return tuple(int(match.group(index)) for index in (1, 2, 3))


def git_tags() -> list[str]:
    result = subprocess.run(
        ["git", "tag", "--list", "v[0-9]*"],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    )
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def matching_patch_numbers(tags: list[str], major: int, minor: int) -> list[int]:
    patches = []
    pattern = re.compile(rf"^v{major}\.{minor}\.(0|[1-9]\d*)$")
    for tag in tags:
        match = pattern.fullmatch(tag)
        if match:
            patches.append(int(match.group(1)))
    return patches


def append_github_outputs(path: Path, outputs: dict[str, str]) -> None:
    with path.open("a", encoding="utf-8") as handle:
        for key, value in outputs.items():
            handle.write(f"{key}={value}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Compute the next stable patch release version.")
    parser.add_argument("--version-file", default="VERSION.txt", type=Path)
    parser.add_argument("--write", action="store_true", help="Write the computed version to VERSION.txt.")
    parser.add_argument("--github-output", type=Path, help="Append outputs for GitHub Actions.")
    args = parser.parse_args()

    major, minor, current_patch = read_version(args.version_file)
    patches = matching_patch_numbers(git_tags(), major, minor)
    next_patch = max([current_patch, *patches]) + 1
    version = f"{major}.{minor}.{next_patch}"
    tag = f"v{version}"

    if args.write:
        args.version_file.write_text(f"{version}\n", encoding="utf-8")

    outputs = {
        "version": version,
        "tag": tag,
    }
    for key, value in outputs.items():
        print(f"{key}={value}")
    if args.github_output:
        append_github_outputs(args.github_output, outputs)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
