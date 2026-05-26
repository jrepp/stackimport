# Code Quality And Vendor Boundary Remediation Plan

## Purpose

This plan tracks the cross-sectional code review findings around parser
correctness, library API behavior, and vendored support-library boundaries. The
goal is to turn the current importer into a more reliable embeddable library
without losing the corpus-driven reverse-engineering workflow.

The work should stay incremental. Each phase should land with focused tests and,
when parser behavior changes, updates to the relevant format documentation under
`Stack File Format/`.

## Current Review Findings

1. Truncated block payloads can be treated as successful parser stops.
2. The C API platform abstraction is incomplete; core import still uses direct
   C and C++ filesystem APIs.
3. Allocation failures in library code can abort the process instead of
   returning an API status.
4. Resource conversion logic is duplicated between the SAX resource walker and
   package exporter, with feature drift.
5. The core library includes and compiles private `resource_dasm` implementation
   files directly.
6. Static targets expose too much vendor surface to consumers.
7. Source manifest offsets are manually tracked and can drift after skipped
   blocks.
8. Legacy `CBuf` dummy-buffer behavior hides bounds and parser errors.

## Guiding Principles

- Preserve raw evidence. Malformed stacks and undecodable resources should
  produce diagnostics and provenance, not silent substitutions.
- Keep public C ABI behavior explicit. Library calls should return statuses or
  callback results rather than terminating the process.
- Isolate vendors behind narrow adapters. StackImport-owned code should depend on
  stable adapter interfaces, not private vendor source layout.
- Prefer one conversion pipeline with multiple sinks. CLI package writing, C
  callbacks, and corpus indexing should consume the same parsed/converted
  resource events.
- Avoid large mechanical rewrites without test coverage. Replace legacy behavior
  one parser boundary at a time.

## Phase 1: Parser Error Semantics

### Tasks

- Split `IBlockOutput::on_block` stop reasons into explicit outcomes:
  continue, caller stopped, and hard failure.
- Make streaming `BlockParser::parse` return `TruncatedPayload` when a callback
  cannot read the declared payload.
- Make `parse_view` validate `pos + payload_bytes <= buf.size` before emitting a
  block or advancing.
- Record `TAIL` and skipped `FREE` blocks in source manifest data, or document
  why they are intentionally excluded.
- Change `CStackBlockOutput` to use `block.file_offset` instead of its own
  manual `position_`.

### Tests

- Unit test a data fork with a header declaring more payload bytes than exist.
- Unit test a zero-copy scan over the same truncated payload.
- Unit test manifest offsets with a `FREE` block before a valid block.
- Corpus spot check against a known-good stack to confirm unchanged normal
  output.

### Lateral Audit

- Check all parser callbacks for boolean returns that currently mean both
  "stop" and "error".
- Check CLI `--scan` behavior for malformed stacks; it should report a parse
  error, not a partial success.

## Phase 2: Platform I/O Boundary

### Status

- In progress.
- Package/resource output paths now use the internal platform file helpers, and
  PNG writing uses `stbi_write_png_to_func`.
- Removed the unused `FileStackReader` helper from `include/stackimport_sax.hpp`
  so the internal SAX parser no longer carries its own direct `fopen` reader.
- Added a resource-fork platform callback regression that supplies
  `..namedfork/rsrc` bytes through the configured platform and verifies converted
  ICON output also writes through platform callbacks.
- Remaining direct filesystem calls are either CLI/default-platform/test
  shims or still need a deliberate CLI-local versus library-backed decision.
- Remaining work: resource-fork input callback coverage, failing-write result
  coverage for every output kind beyond the current package-level checks,
  path-separator behavior, and close/partial write diagnostics.

### Tasks

- Introduce internal reader and writer abstractions backed by
  `stackimport_platform`.
- Move stack file input, resource fork input, text output, binary output, and
  directory creation behind those abstractions.
- Replace path-writing vendor calls with byte-producing helpers where practical.
  For PNG output, prefer `stbi_write_png_to_func` so package writing goes through
  the platform writer.
- Audit direct uses of `fopen`, `std::ifstream`, `std::ofstream`, `mkdir`,
  `stat`, and `fwrite` outside CLI-only code.
- Decide whether CLI `--scan` and `--rom` are library-backed workflows or
  intentionally CLI-local utilities, then isolate them accordingly.

### Tests

- Add a platform test whose file callbacks count opens/writes and reject direct
  filesystem access.
- Add a failing-write callback test that verifies import returns failure and
  reports the affected output.
- Done: add a resource-fork callback test once resource-fork input is platform-backed.

### Lateral Audit

- Check output package creation on Windows path separators.
- Check partial writes and close failures for every output kind.
- Check whether `errno == EEXIST` is safe after platform callbacks, or whether
  the platform API needs explicit result codes.

## Phase 3: Allocation And Error Propagation

### Status

- Mostly complete for owned library paths.
- Current audit finds no `abort`, `std::abort`, or `exit` calls in owned C/C++
  library code; remaining `new` use is placement-new into caller/context
  storage.
- C API allocation failures now surface as
  `STACKIMPORT_STATUS_ALLOCATION_FAILED`, with tests covering platform allocator
  and RapidJSON allocation failure paths.
- Public C header now documents that caller callbacks must not unwind across the
  C ABI boundary and should report failures through callback return values.
- Remaining work: extend allocation-failure coverage to every resource
  conversion branch, especially optional sound/disassembly paths.

### Tasks

- Done: replace library-path `std::abort()` allocation failures with explicit error
  propagation.
- Add an internal import error object that carries status, diagnostic text, and
  optional source location.
- Done: make `CBuf`, `PlatformAllocator`, RapidJSON allocation, and resource
  conversion report allocation failure without terminating.
- Done: define API behavior when callbacks throw is not relevant because exceptions
  are disabled; document that callbacks must not unwind.

### Tests

- Add a custom allocator that fails after N allocations and verify
  `STACKIMPORT_STATUS_ALLOCATION_FAILED`.
- Add allocation-failure coverage for block ingestion, JSON writing, and sound
  conversion.

### Lateral Audit

- Search for all `abort`, `exit`, unchecked `new`, and allocation assumptions.
- Confirm strict `-fno-exceptions` remains compatible with all owned code after
  replacing abort paths.

## Phase 4: Unified Resource Pipeline

### Status

- Partially complete.
- The C callback path and package writer both use `ResourcePayload` for native
  and converted payload metadata, and package output now emits converted ICON,
  CURS, PAT#, `snd `, PLTE, 68K, and PowerPC artifacts from
  `StackImportResourceFork.cpp`.
- Still open: conversion logic is duplicated between
  `include/stackimport_sax.hpp` and `StackImportResourceFork.cpp`; the SAX path
  only covers a subset of converted resource families.
- Remaining work: move conversion handlers behind one owned event/converter
  pipeline and make SAX/resource walking delegate to it or remove the duplicate
  conversion surface.

### Tasks

- Partly done: define a `ResourceEvent` or equivalent owned domain model:
  native resource, converted payload, conversion diagnostic, output artifact, and
  summary metadata.
- Move ICON, CURS, PAT#, PLTE, `snd `, 68K disassembly, and PowerPC disassembly
  into resource converter handlers.
- Let package output, C resource callbacks, and future corpus indexing consume
  the same event stream.
- Remove duplicate conversion code from `include/stackimport_sax.hpp` and
  `StackImportResourceFork.cpp`, or make one delegate to the other.
- Preserve callback filtering semantics so expensive conversions can still be
  skipped when the caller does not want them.

### Tests

- One fixture resource fork should exercise native plus converted outputs through
  both the C API callback path and package writer path.
- Add regression checks that SAX/resource walking and package export expose the
  same converted resource families.
- Add tests for conversion failure diagnostics that do not abort the rest of the
  resource fork.

### Lateral Audit

- Check resource names and type codes for MacRoman/high-bit byte preservation.
- Check output filename sanitization for collisions after percent encoding.
- Check payload lifetime documentation against actual callback storage.

## Phase 5: Vendor Adapter Cleanup

### Status

- In progress.
- MACE decoding now goes through `StackImportMaceResourceDasmAdapter`; the
  private `resource_dasm/src/AudioCodecs.hh` include is isolated to the narrow
  adapter source under `vendor/`.
- Verified default vendor-tools-on and `STACKIMPORT_BUILD_VENDOR_TOOLS=OFF`
  configurations build and pass tests after the adapter split.
- Remaining work: resource-fork, disassembly, PNG, and WAV adapter target
  boundaries; broader license/provenance audit.

### Tasks

- Done: stop including private vendor paths such as
  `vendor/resource_dasm/src/AudioCodecs.hh` from StackImport-owned sources.
- Create narrow adapter targets for:
  - resource fork parsing and small resource decoders from `rsrcd`;
  - Done: MACE decoding from `resource_dasm` or an owned codec extraction;
  - disassembly support from `resource_dasm`;
  - PNG encoding through stb;
  - WAV writing through owned code or `dr_wav`.
- Decide whether `vendor_snd2wav` is still needed. If not used by the core
  importer, remove it from default link targets and document it as legacy
  reference material or delete it in a separate cleanup.
- Move vendor-specific compiler suppressions to vendor adapter targets.
- Update `vendor/INDEX.md` for any dependency role changes.

### Tests

- Done: build with `STACKIMPORT_BUILD_VENDOR_TOOLS=ON` and `OFF`.
- Done: build shared and static targets with warning-as-error settings.
- Add adapter-level tests around MACE decode, PNG bytes, and disassembly fallback
  behavior.

### Lateral Audit

- Check license/provenance notes for any copied codec code.
- Check Homebrew/release build behavior with vendor tools disabled.
- Check that installed headers do not require private vendor include paths unless
  explicitly part of the public API.

## Phase 6: Public Target And Install Hygiene

### Status

- In progress.
- Vendor libraries are linked privately by the core targets, and installation
  still publishes only `stackimport_c.h`.
- Target include directories now distinguish build-tree and install-tree include
  interfaces instead of exporting the source root as the install interface.
- The existing install-tree `pkg-config` smoke test passes in default,
  warning-as-error, and vendor-tools-off builds.
- Added a shared-library symbol smoke test that checks the exported C API
  surface with `nm` when available.
- `include/stackimport_sax.hpp` remains an internal/experimental header: it is
  used by the CLI/core implementation but is not installed or documented as a
  public API.
- Remaining work: add CMake package/export coverage if embedders need imported
  targets, expand static/shared smoke tests, and finish ABI/version review.

### Tasks

- Make vendor libraries `PRIVATE` where they are implementation details.
- Done: split public headers from internal headers in CMake include directories.
- Done: install only supported public headers.
- Done: decide whether `include/stackimport_sax.hpp` is a supported public C++ API or
  an internal experimental header. If public, give it stable install and
  dependency rules; if internal, remove it from public-facing docs.
- Review `pkg-config` and CMake package needs for embedders.

### Tests

- Add an install-tree smoke test that compiles a minimal C program using only
  `stackimport_c.h` and `pkg-config`.
- Add a static-link smoke test if static archive installation remains supported.
- Done: add a shared-library visibility check for exported C symbols.

### Lateral Audit

- Check ABI version bump requirements for any C API behavior changes.
- Check Windows shared-library macro behavior after install/export changes.

## Phase 7: Legacy Buffer Hardening

### Status

- In progress.
- Added `CBuf::checked_buf` accessors that return `nullptr` on out-of-bounds
  reads or failed copy-on-write allocation instead of returning the legacy dummy
  buffer.
- The central `ReadBE*` helpers in `CStackFile.cpp` now use checked byte views,
  so new big-endian parser reads no longer depend on dummy-buffer substitution.
- Remaining work: convert high-risk block parsers to checked reads with
  diagnostics, preserve malformed raw blocks before warnings, and remove or
  quarantine remaining mutable dummy-buffer call paths.

### Tasks

- Done: introduce checked byte-view helpers for parser code that still uses
  `CBuf`.
- Convert high-risk block parsers from dummy-buffer reads to explicit
  bounds-checked parsing.
- Preserve raw block dumps for malformed blocks before returning parse warnings.
- Phase out mutable static dummy buffers from new code paths.

### Tests

- Add focused malformed-block fixtures for STAK, STBL, BMAP, CARD/BKGD parts,
  PAGE, PRST, and PRFT.
- Add corpus subset runs before and after each converted parser family.

### Lateral Audit

- Check all `ReadBE*` call sites for prevalidated lengths.
- Check all loops over block-contained counts for multiplication and offset
  overflow.

## Suggested Implementation Order

1. Fix parser truncation and manifest offsets.
2. Add tests that lock in current API behavior and malformed-input behavior.
3. Introduce platform-backed I/O abstractions and move direct filesystem access.
4. Replace allocation abort paths with status propagation.
5. Consolidate resource conversion into one event pipeline.
6. Isolate vendor code behind adapter targets.
7. Clean up public target/install surface.
8. Harden `CBuf`-based parsers incrementally.

## Definition Of Done

- Default build and tests pass:
  `cmake -S . -B build && cmake --build build &&
  ctest --test-dir build --output-on-failure`.
- Warning-as-error pass succeeds:
  `cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Debug
  -DSTACKIMPORT_STRICT_WARNINGS=ON
  -DSTACKIMPORT_WARNINGS_AS_ERRORS=ON
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -Wdev --warn-uninitialized &&
  cmake --build build-cmake &&
  ctest --test-dir build-cmake --output-on-failure`.
- Vendor-tool on/off configurations build.
- C API consumers can import without direct filesystem or allocation behavior
  escaping the configured platform callbacks.
- Malformed inputs produce explicit diagnostics and statuses.
- Vendor dependency roles and any format behavior changes are documented.
