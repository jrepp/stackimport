#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path


SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\."
    r"(0|[1-9]\d*)\."
    r"(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9A-Za-z-][0-9A-Za-z-]*))*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


def render_formula(repository: str, version: str, sha256: str) -> str:
    if not SEMVER_RE.fullmatch(version):
        raise ValueError(f"invalid SemVer version: {version!r}")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", sha256):
        raise ValueError("sha256 must be 64 hexadecimal characters")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError(f"invalid GitHub repository: {repository!r}")

    tag = f"v{version}"
    return f'''# typed: false
# frozen_string_literal: true

class Stackimport < Formula
  desc "HyperCard stack importer"
  homepage "https://github.com/{repository}"
  url "https://github.com/{repository}/archive/refs/tags/{tag}.tar.gz"
  sha256 "{sha256.lower()}"
  license "MIT"
  head "https://github.com/{repository}.git", branch: "master"

  depends_on "cmake" => :build

  def install
    system "cmake", "-S", ".", "-B", "build",
                    "-DSTACKIMPORT_BUILD_TESTS=OFF",
                    "-DSTACKIMPORT_BUILD_VENDOR_TESTS=OFF",
                    *std_cmake_args
    system "cmake", "--build", "build", "--target", "install"
  end

  test do
    assert_match "Syntax is", shell_output("#{{bin}}/stackimport 2>&1", 2)

    (testpath/"smoke.c").write <<~C
      #include <stackimport_c.h>
      int main(void) {{
        return stackimport_api_version() == STACKIMPORT_API_VERSION ? 0 : 1;
      }}
    C
    system ENV.cc, "smoke.c", "-I#{{include}}", "-L#{{lib}}",
                   "-lstackimport_c", "-Wl,-rpath,#{{lib}}", "-o", "smoke"
    system "./smoke"

    if OS.mac?
      assert_path_exists prefix/"Frameworks/StackImport.framework/Headers/stackimport_c.h"

      (testpath/"framework_smoke.c").write <<~C
        #include <StackImport/stackimport_c.h>
        int main(void) {{
          return stackimport_api_version() == STACKIMPORT_API_VERSION ? 0 : 1;
        }}
      C
      system ENV.cc, "framework_smoke.c", "-F#{{prefix}}/Frameworks",
                     "-framework", "StackImport", "-Wl,-rpath,#{{prefix}}/Frameworks",
                     "-o", "framework_smoke"
      system "./framework_smoke"
    end
  end
end
'''


def main() -> int:
    parser = argparse.ArgumentParser(description="Render the Homebrew formula for stackimport.")
    parser.add_argument("--repository", default="jrepp/stackimport", help="GitHub owner/repo for source archives.")
    parser.add_argument("--version", required=True, help="Release SemVer, without leading v.")
    parser.add_argument("--sha256", required=True, help="SHA-256 of the GitHub source archive.")
    parser.add_argument("--output", required=True, type=Path, help="Formula path to write.")
    args = parser.parse_args()

    formula = render_formula(args.repository, args.version, args.sha256)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(formula, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
