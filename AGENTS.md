# AGENTS.md

Guidance for coding agents working in this repository.

## Core Build And Test Workflow

Use the CMake build first:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The compatibility Makefile wraps the same flow:

```sh
make stackimport
make test
```

Strict warnings are enabled by default. Before handing off C++ changes, also run
a warning-as-error pass:

```sh
cmake -S . -B build-cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSTACKIMPORT_STRICT_WARNINGS=ON \
  -DSTACKIMPORT_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -Wdev --warn-uninitialized
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

The project requires C17/C++23-capable compilers. If local static-analysis tools
are installed, use the CMake hooks after the compiler warning pass:

```sh
cmake -S . -B build-analysis \
  -DSTACKIMPORT_ENABLE_CLANG_TIDY=ON \
  -DSTACKIMPORT_ENABLE_CLANG_STATIC_ANALYZER=ON \
  -DSTACKIMPORT_ENABLE_CPPCHECK=ON \
  -DSTACKIMPORT_ENABLE_INCLUDE_WHAT_YOU_USE=ON
cmake --build build-analysis
```

Do not treat missing analyzer tools as a reason to skip the compiler/CMake
warning checks.

## Documentation Site Workflow

The documentation site lives under `website/` and is built with Astro/Starlight.
When changing files under `docs/` or `website/`, validate both formatting and the
site build from the `website/` directory:

```sh
cd website
pnpm lint
pnpm build
```

The site package requires Node 24 or newer and declares `pnpm` as its package
manager. If `pnpm` is not available on `PATH` but dependencies are already
installed, use the local binaries directly as a fallback:

```sh
cd website
./node_modules/.bin/prettier --check . ../docs
./node_modules/.bin/astro build
```

## Workflow References

Repeatable project workflows live under `docs/workflows/`. Use them as the
canonical checklist for common tasks before making changes:

- `docs/workflows/cpp-change.mdx` for importer, parser, C API, and C++ changes.
- `docs/workflows/docs-site-change.mdx` for `docs/` and `website/` changes.
- `docs/workflows/format-reverse-engineering.mdx` for evidence-driven format
  investigation.
- `docs/workflows/corpus-import-run.mdx` for batch importer runs and report
  review.
- `docs/workflows/cli-test-acceptance-scenarios.mdx` for the CLI acceptance
  overview, with mode-specific checks split into `cli-help-acceptance.mdx`,
  `cli-import-acceptance.mdx`, `cli-scan-acceptance.mdx`, and
  `cli-rom-acceptance.mdx`.
- `docs/workflows/release-version-update.mdx` for SemVer and release-surface
  updates.

## Versioning Discipline

`VERSION.txt` is the source of truth for the library version. Keep it SemVer.
The generated `stackimport_version.h` exposes the compile-time version macros,
and the public C ABI exposes matching runtime version queries. While the library
is pre-1.0, any breaking public API or ABI change must bump the minor version;
major feature work must bump the major version; compatible additions and fixes
may bump the patch version.

## Stack Import Workflow

`stackimport` reads a HyperCard stack and emits a JSON/media package. Useful
manual runs:

```sh
build/stackimport /path/to/Stack
build/stackimport --dumprawblocks /path/to/Stack
build/stackimport --rawgraphics /path/to/Stack
```

Use `--dumprawblocks` when investigating an unknown or partially understood
structure. Raw `.data` files can then be compared against the local format notes
in `Stack File Format/`.

For corpus runs, use:

```sh
scripts/import_all_stacks.py --stackimport-bin build/stackimport
```

`scripts/import_all_stacks.py` carries a `#!/usr/bin/env -S uv run --script`
shebang, so run it directly from the repository root; `uv` supplies the script
runtime. The script extracts `.sit` and `.hqx` archives with `unar`, fingerprints
extracted files, runs `stackimport` only against files classified as stacks, and
writes logs, reports, output packages, and a SQLite index under `import-runs/`.
Code-resource disassembly is integrated into the core importer for
XCMD/XFCN/xcmd/xfcn resources:

```sh
scripts/import_all_stacks.py --stackimport-bin build/stackimport
```

The core importer writes embedded disassembly as
`resource-disassembly/*.s` inside each generated `.xstk` package, and indexes
those files as code-resource outputs. The legacy external `resource_dasm` pass
is available with `--disassemble-code-resources` when comparing old output.

For embedded output conversion, run the converter against a completed importer
database:

```sh
scripts/convert_embedded_files.py --run-db import-runs/<run-id>/run.db
```

The converter currently turns embedded PBM/P4 images into PNG files, writes a
`embedded_conversions` table to the run database, emits
`embedded-conversions/embedded-conversions.json`, and writes per-file
`*.provenance.json` sidecars. Preserve those sidecars; they record stack,
archive, source hash, converter, target hash, and conversion status metadata.

The default corpus input directory is:

```text
/Users/jrepp/d/pantechnicon/stacks
```

Sample data comes from the Pantechnicon stack collection:

```text
https://hypercard.org/Pantechnicon/Stacks/
```

Mirror new sample data slowly and in small batches. Preserve archive boundaries
and keep non-stack files in the mirror; the importer is responsible for
classifying and skipping them.

## Reverse Engineering Technique

Prefer contrastive analysis over single-file guesses:

1. Run a focused subset through `scripts/import_all_stacks.py`.
2. Review the `report_summary`, `report_diagnostics`, and related report tables
   in the run SQLite database, plus captured logs in the run directory.
3. Separate extraction/classification issues from parser issues.
4. Compare successful and failing stacks with similar block/resource layouts.
5. Use `--dumprawblocks` on representative inputs.
6. Reconcile byte offsets against `Stack File Format/README.md` and the topic
   notes under `Stack File Format/*/`.
7. Update the parser and the documentation together.
8. Re-run the same subset before broadening the corpus.

The importer records status lines, warnings, errors, block/resource counts,
classified extracted files, parsed stack summaries, output files, media
references, stack objects, parts, content, and aggregate stats. Use this indexed
data to find patterns across stacks instead of hand-inspecting one output at a
time.

## Format Documentation Expectations

`Stack File Format/README.md` is the index of current understanding. Keep it
accurate when parser behavior changes:

- Add new block/resource coverage to the status tables.
- Record whether understanding is high, medium, or low.
- Document unknown/reserved bytes instead of silently skipping them.
- Record corpus evidence when a change comes from observed stacks.
- Link topic-specific details to the relevant files under `Stack File Format/`.

When creating new format files or derived documentation, include as much
conversion provenance metadata as possible. Prefer a short front-matter or
introductory section that records:

- Source material and URL or local path.
- Retrieval or conversion date.
- Tooling used to convert, transcribe, normalize, or summarize the source.
- Input file checksums and byte sizes when local source files are available.
- Output file path and relationship to the original source.
- Known omissions, lossy conversions, OCR/transcription uncertainty, or manual
  interpretation.
- Corpus runs, stack paths, archive names, logs, and SQLite/report locations
  that support any reverse-engineered conclusions.

Known partial areas should stay explicit. At the time of writing, the project
still treats some print fields, reserved layer/page bytes, AddColor unknown
object types, and external code resources as partially understood or raw export
data.

## Legacy Mac Encoding Notes

Classic Mac stack, archive, and resource tooling often emits bytes that are not
valid UTF-8. Resource names, file names, Pascal strings, script text, and DeRez
output may be MacRoman or another legacy Mac encoding. Treat undecodable bytes as
format evidence, not as disposable noise.

When scanning resource forks or external tools such as `DeRez`, avoid default
UTF-8 text decoding. Capture bytes first, decode with MacRoman when the source is
classic Mac data, and preserve the original bytes or checksums in generated
reports. A UTF-8 `UnicodeDecodeError` can otherwise make valid XCMD/XFCN headers
look absent.

For `stackimport` itself, prefer explicit conversion boundaries:

- Parse binary fields as bytes until a format rule identifies them as text.
- Decode known classic Mac text with a named converter such as MacRoman to UTF-8.
- Preserve raw byte offsets and raw payload exports for uncertain strings.
- Record encoding assumptions in JSON/report metadata when generated text was
  transcoded.
- Add regression cases for high-bit resource names and strings so corpus scans
  do not silently drop legacy Mac data.

## Implementation Pointers

- Block stream parsing and dispatch live in `CStackFile.cpp`.
- Shared stack block state and block identifiers live in `CStackFile.h`.
- WOBA bitmap decoding lives in `woba.cpp` and `picture.cpp`.
- Big-endian helpers live in `byteutils.cpp` and `byteutils.h`.
- Corpus extraction, fingerprinting, import execution, and report indexing live
  in `scripts/import_all_stacks.py`.
- `vendor/snd2wav/` is the local copy of the helper project used for sampled sound
  conversion experiments.

Keep changes narrowly scoped. If there are unrelated local modifications, leave
them alone unless they directly block the requested work.
