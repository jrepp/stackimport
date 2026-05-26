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
- Add a resource-fork callback test once resource-fork input is platform-backed.

### Lateral Audit

- Check output package creation on Windows path separators.
- Check partial writes and close failures for every output kind.
- Check whether `errno == EEXIST` is safe after platform callbacks, or whether
  the platform API needs explicit result codes.

## Phase 3: Allocation And Error Propagation

### Tasks

- Replace library-path `std::abort()` allocation failures with explicit error
  propagation.
- Add an internal import error object that carries status, diagnostic text, and
  optional source location.
- Make `CBuf`, `PlatformAllocator`, RapidJSON allocation, and resource
  conversion report allocation failure without terminating.
- Define API behavior when callbacks throw is not relevant because exceptions
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

### Tasks

- Define a `ResourceEvent` or equivalent owned domain model:
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

### Tasks

- Stop including private vendor paths such as
  `vendor/resource_dasm/src/AudioCodecs.hh` from StackImport-owned sources.
- Create narrow adapter targets for:
  - resource fork parsing and small resource decoders from `rsrcd`;
  - MACE decoding from `resource_dasm` or an owned codec extraction;
  - disassembly support from `resource_dasm`;
  - PNG encoding through stb;
  - WAV writing through owned code or `dr_wav`.
- Decide whether `vendor_snd2wav` is still needed. If not used by the core
  importer, remove it from default link targets and document it as legacy
  reference material or delete it in a separate cleanup.
- Move vendor-specific compiler suppressions to vendor adapter targets.
- Update `vendor/INDEX.md` for any dependency role changes.

### Tests

- Build with `STACKIMPORT_BUILD_VENDOR_TOOLS=ON` and `OFF`.
- Build shared and static targets with warning-as-error settings.
- Add adapter-level tests around MACE decode, PNG bytes, and disassembly fallback
  behavior.

### Lateral Audit

- Check license/provenance notes for any copied codec code.
- Check Homebrew/release build behavior with vendor tools disabled.
- Check that installed headers do not require private vendor include paths unless
  explicitly part of the public API.

## Phase 6: Public Target And Install Hygiene

### Tasks

- Make vendor libraries `PRIVATE` where they are implementation details.
- Split public headers from internal headers in CMake include directories.
- Install only supported public headers.
- Decide whether `include/stackimport_sax.hpp` is a supported public C++ API or
  an internal experimental header. If public, give it stable install and
  dependency rules; if internal, remove it from public-facing docs.
- Review `pkg-config` and CMake package needs for embedders.

### Tests

- Add an install-tree smoke test that compiles a minimal C program using only
  `stackimport_c.h` and `pkg-config`.
- Add a static-link smoke test if static archive installation remains supported.
- Add a shared-library visibility check for exported C symbols.

### Lateral Audit

- Check ABI version bump requirements for any C API behavior changes.
- Check Windows shared-library macro behavior after install/export changes.

## Phase 7: Legacy Buffer Hardening

### Tasks

- Introduce checked byte-view helpers for parser code that still uses `CBuf`.
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
