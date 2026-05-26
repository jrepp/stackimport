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
- Resource reference conversion, native/converted payload construction, and
  callback filtering/delivery now share helpers between the SAX walker and
  package exporter.
- Resource domain types and callback helpers moved to
  `StackImportResourceTypes.h`, and built-in zero-allocation resource transforms
  for ICON, CURS, and PAT# plus the typed PLTE parser/JSON serializer moved to
  `StackImportResourceTransforms.cpp`. `snd ` to WAV conversion now also emits
  through this transform surface, as do 68K and PowerPC code-resource
  disassembly payloads.
  `ResourceForkParser` and the package exporter now share those transform rules
  for callback payload delivery; the package exporter also writes ICON, CURS,
  PAT#, PLTE, `snd `, and code-resource disassembly artifacts from the same
  transform payloads instead of decoding, serializing, converting, or
  disassembling them a second time.
- Still open: promote the transform surface from payload-only callbacks to a
  fuller event model for summaries, diagnostics, artifact metadata, and future
  resource families.
- Added `Stack File Format/ResourceForkCoverage.md` as the ownership map for
  StackImport/rsrcd core parsers, resource_dasm fold/adapt candidates, and broad
  fallback decoders.
- AddColor `HCbg`/`HCcd` now uses the existing rsrcd typed parser in the
  transform pipeline and emits JSON package artifacts.
- Classic text resources `STR `, `STR#`, and `TEXT` now use rsrcd typed text
  views plus shared MacRoman-to-UTF-8 transforms; package output writes `STR `
  and `TEXT` as UTF-8 text artifacts and `STR#` as JSON to preserve string
  boundaries.
- `TwCS` text resources now have an rsrcd typed parser and shared JSON
  transform for unencrypted MacRoman string lists; encrypted entries currently
  report a parse diagnostic and preserve native bytes.
- Additional bounded image resources `ICN#` and `PAT ` now use rsrcd image
  helpers and shared RGBA transforms, with PNG package artifacts.
- `SICN` small-icon lists now use rsrcd bounded image decoding and shared
  RGBA transforms, with PNG package artifacts.
- Indexed icon bitmaps `icl4`, `icl8`, `icm4`, `icm8`, `ics4`, and `ics8` now
  use rsrcd bounded image decoding and shared RGBA transforms, with PNG package
  artifacts. Parallel mask composition remains future event-stream work.
- Monochrome icon lists `icm#` and `ics#` now use rsrcd bounded decoding and
  shared RGBA transforms, including bitmap/mask composition when exactly two
  images are present.
- `cfrg` Code Fragment Manager metadata now has an rsrcd typed parser and shared
  JSON transform for fragment records, names, locations, and extension bytes.
- `MBAR` menu bar resources now have an rsrcd typed parser and shared JSON
  transform for menu resource ID lists.
- Finder/UI metadata resources `ALRT`, `FREF`, and `BNDL` now have rsrcd typed
  parsers and shared JSON transforms for alert parameters, file references, and
  bundle mappings.
- `ROv#` ROM override lists now have an rsrcd typed parser and shared JSON
  transform for ROM version plus overridden resource type/id entries.
- `RSSC` resources now have an rsrcd typed metadata parser and shared JSON
  transform for export offsets and code size; disassembly remains future
  code-resource adapter work.
- `TxSt` text style resources now have an rsrcd typed parser and shared JSON
  transform for font style, size, RGB color, and font name.
- `styl` style-run resources now have an rsrcd typed parser and shared JSON
  transform for run offsets, font metrics, style flags, font size, and RGB
  color.
- `KCHR` keyboard character maps now have an rsrcd typed parser and shared JSON
  transform for modifier table indexes, character tables, and dead-key
  completions.
- Simple template-backed metadata resources `RECT` and `TOOL` now have rsrcd
  typed parsers and shared JSON transforms.
- `PICK` picker resources now have an rsrcd typed parser and shared JSON
  transform for picker settings and referenced resource IDs.
- `KBDN` and `PAPA` resources now have rsrcd typed parsers and shared JSON
  transforms for keyboard names and printer parameters.
- `LAYO` layout resources now have an rsrcd typed parser and shared JSON
  transform for font/layout metrics and rectangles.
- `CODE` resources now have an rsrcd typed metadata parser and shared JSON
  transform for CODE 0 jump tables and near/far segment headers; disassembly
  remains future code-resource adapter work.
- `DRVR` resources now have an rsrcd typed metadata parser and shared JSON
  transform for driver flags, entry labels, names, and code size.
- `dcmp` resources now have an rsrcd typed metadata parser and shared JSON
  transform for entry labels, PC offset, and code size.
- `vers` metadata now has an rsrcd typed parser and shared JSON transform that
  preserves raw numeric version fields while decoding version strings.
- `clut`, `CTBL`, `actb`, `cctb`, `dctb`, `fctb`, and `wctb` color tables now
  have an rsrcd typed parser and shared JSON transform for seed, flags, and
  16-bit RGB entries.
- `pltt` palettes now have an rsrcd typed parser and shared JSON transform for
  16-bit RGB entries.
- `ppat` and `ppt#` pixel patterns now have rsrcd typed metadata parsers and
  shared JSON transforms for pattern headers, monochrome bits, list offsets, and
  pixmap metadata where present.
- `SIZE` metadata now has an rsrcd typed parser and shared JSON transform for
  application flags plus preferred/minimum memory sizes.
- `finf` font metadata now has an rsrcd typed parser and shared JSON transform
  for font id, style flags, and size triples.
- `CNTL`, `DLOG`, and `WIND` UI metadata now have rsrcd typed parsers and
  shared JSON transforms for bounds, state flags, proc ids, refcons, titles, and
  auto-position fields where present.
- `MENU` metadata now has an rsrcd typed parser and shared JSON transform for
  menu title, enabled flags, and item records.
- `DITL` metadata now has an rsrcd typed parser and shared JSON transform for
  item bounds, item kind, enabled state, text info, and referenced resource IDs.
- Heavy `PICT` rendering is kept behind `StackImportResourceDasmPictAdapter`;
  core StackImport code sees only a narrow PNG payload transform and still
  preserves native bytes when conversion fails.
- Remaining work: expand the shared transform interface into one owned
  event/converter pipeline for metadata, diagnostics, and artifacts beyond the
  current image-payload transform cases.

### Tasks

- Partly done: define a `ResourceEvent` or equivalent owned domain model:
  native resource, converted payload, conversion diagnostic, output artifact, and
  summary metadata.
- Done for image resources: move ICON, CURS, and PAT# transforms into
  `StackImportResourceTransforms.cpp`, and route package PNG artifact writing
  through those transform payloads.
- Done for PLTE metadata: move typed PLTE parsing and JSON serialization into
  `StackImportResourceTransforms.cpp`, and route package JSON artifact writing
  through that transform payload.
- Done for `snd ` audio: move WAV conversion into
  `StackImportResourceTransforms.cpp`, and route package WAV artifact writing
  through that transform payload.
- Done for code resources: move 68K and PowerPC disassembly into
  `StackImportResourceTransforms.cpp`, and route package `.s` artifact writing
  through that transform payload.
- Done for core text resources: parse `STR `, `STR#`, and `TEXT` through rsrcd
  text helpers and route package text/JSON artifact writing through those
  transform payloads.
- Done for `TwCS` text resources: parse unencrypted string lists through rsrcd
  and route package JSON artifact writing through the shared transform payload.
- Done for additional bounded image resources: decode `ICN#` and `PAT ` through
  rsrcd image helpers and route package PNG artifact writing through those
  transform payloads.
- Done for `SICN` small-icon lists: decode each 16x16 icon through rsrcd image
  helpers and route package PNG artifact writing through the shared transform
  payload.
- Done for indexed icon bitmaps: decode `icl4`, `icl8`, `icm4`, `icm8`, `ics4`,
  and `ics8` through rsrcd image helpers and route package PNG artifact writing
  through the shared transform payload.
- Done for monochrome icon lists: decode `icm#` and `ics#` through rsrcd image
  helpers and route package PNG artifact writing through the shared transform
  payload.
- Done for `cfrg` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for `MBAR` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for Finder/UI metadata: parse `ALRT`, `FREF`, and `BNDL` through rsrcd
  and route package JSON artifact writing through the shared transform payload.
- Done for `ROv#` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for `RSSC` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for `TxSt` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for `styl` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for `KCHR` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for simple template-backed metadata: parse `RECT` and `TOOL` through
  rsrcd and route package JSON artifact writing through the shared transform
  payload.
- Done for `PICK` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for string/printer metadata: parse `KBDN` and `PAPA` through rsrcd and
  route package JSON artifact writing through the shared transform payload.
- Done for `LAYO` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for `CODE` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for `DRVR` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for `dcmp` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for `vers` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for color tables: parse `clut`, `CTBL`, `actb`, `cctb`, `dctb`,
  `fctb`, and `wctb` through rsrcd and route package JSON artifact writing
  through the shared transform payload.
- Done for `pltt` palettes: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for pixel pattern metadata: parse `ppat` and `ppt#` through rsrcd and
  route package JSON artifact writing through the shared transform payload.
- Done for `SIZE` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for `finf` font metadata: parse through rsrcd and route package JSON
  artifact writing through the shared transform payload.
- Done for fixed-record UI metadata: parse `CNTL`, `DLOG`, and `WIND` through
  rsrcd and route package JSON artifact writing through the shared transform
  payload.
- Done for `MENU` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Done for `DITL` metadata: parse through rsrcd and route package JSON artifact
  writing through the shared transform payload.
- Promote transform handlers from payload callbacks to a richer resource-event
  stream that can carry summaries, diagnostics, artifact metadata, and future
  typed resource outputs without adding ad hoc package-export branches.
- Use `Stack File Format/ResourceForkCoverage.md` to choose whether each new
  resource type belongs in rsrcd core, a narrow adapter, or raw-preservation
  fallback.
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
- For every resource type below, audit the full consumer chain when converted
  support is added: package artifact writer, C resource callbacks and payload
  flags, corpus report/index tables, embedded conversion manifests/provenance,
  fixture coverage, and any CLI summary/status output.

### Resource Type Audit Inventory

This inventory is seeded from the local `resource_dasm` type registry plus
StackImport-specific resource coverage. Keep it as the checklist for full
resource-fork coverage. Mark a type done only after parser/transform support and
all consumers listed in the lateral audit above have been updated and tested.

- Current typed StackImport/core coverage:
  `ICON`, `ICN#`, `CURS`, `PAT `, `PAT#`, `PLTE`, `clut`, `CTBL`, `actb`,
  `SICN`, `icm#`, `ics#`, `icl4`, `icl8`, `icm4`, `icm8`, `ics4`, `ics8`,
  `cfrg`, `cctb`, `dctb`, `fctb`, `wctb`, `pltt`, `ppat`, `ppt#`, `HCbg`, `HCcd`, `STR `,
  `STR#`, `TEXT`, `TwCS`, `vers`, `SIZE`, `finf`, `CNTL`, `DLOG`, `WIND`,
  `MENU`, `DITL`, `MBAR`, `ALRT`, `FREF`, `BNDL`, `ROv#`, `PICT`, `snd `,
  `RSSC`, `TxSt`, `RECT`, `TOOL`, `PICK`, `KBDN`, `PAPA`, `XCMD`, `XFCN`,
  `LAYO`, `CODE`, `DRVR`, `dcmp`, `styl`, `KCHR`, `xcmd`, `xfcn`.
- Audit inventory:
  `.mod`, `68k!`, `actb`, `acur`, `ADBS`, `adio`, `AINI`, `ALIS`, `alis`,
  `ALRT`, `APPL`, `atlk`, `audt`, `BNDL`, `boot`, `bstr`, `card`, `cctb`,
  `CDEF`, `cdek`, `cdev`, `CDRV`, `cfrg`, `cicn`, `citt`, `clok`, `clut`,
  `CMDK`, `cmid`, `CMNU`, `cmnu`, `cmtb`, `cmu#`, `CNTL`, `CODE`, `code`,
  `crsr`, `csnd`, `CTBL`, `CTY#`, `CURS`, `dbex`, `dcmp`, `dcod`, `dctb`,
  `dem `, `dimg`, `DITL`, `DLOG`, `DRVR`, `drvr`, `ecmi`, `emid`, `enet`,
  `epch`, `errs`, `ESnd`, `esnd`, `expt`, `FBTN`, `FCMT`, `fctb`, `FDIR`,
  `finf`, `FKEY`, `fld#`, `flst`, `fmap`, `FONT`, `fovr`, `FREF`, `FRSV`,
  `FWID`, `gbly`, `gcko`, `GDEF`, `gdef`, `gnld`, `GNRL`, `gpch`, `h8mk`,
  `hqda`, `hwin`, `ic04`, `ic05`, `ic07`, `ic08`, `ic09`, `ic10`, `ic11`,
  `ic12`, `ic13`, `ic14`, `ich4`, `ich8`, `ich#`, `icl4`, `icl8`, `icm4`,
  `icm8`, `icm#`, `icmt`, `ICN#`, `icns`, `icnV`, `ICON`, `icp4`, `icp5`,
  `icp6`, `ics4`, `ics8`, `icsb`, `icsB`, `ics#`, `ih32`, `il32`, `inbb`,
  `indm`, `info`, `infs`, `INIT`, `inpk`, `inra`, `insc`, `INTL`, `INST`,
  `is32`, `it32`, `itl0`, `itl1`, `ITL1`, `itlb`, `itlc`, `itlk`, `KBDN`,
  `KCHR`, `kcs4`, `kcs8`, `kcs#`, `krnl`, `l8mk`, `LAYO`, `LDEF`, `lmgr`,
  `lodr`, `lstr`, `ltlk`, `mach`, `MACS`, `MADH`, `MADI`, `MBAR`, `MBDF`,
  `mcky`, `MDEF`, `mem!`, `MENU`, `MIDI`, `Midi`, `midi`, `minf`, `mitq`,
  `mntr`, `MOOV`, `MooV`, `moov`, `mstr`, `mst#`, `name`, `ncmp`, `ndlc`,
  `ndmc`, `ndrv`, `NFNT`, `nift`, `nitt`, `nlib`, `nrct`, `nsnd`, `nsrd`,
  `ntrb`, `osl `, `otdr`, `otlm`, `PACK`, `PAPA`, `PAT `, `PAT#`, `PICK`,
  `PICT`, `pltt`, `pnll`, `ppat`, `ppc!`, `ppcc`, `ppci`, `ppct`, `PPic`,
  `ppt#`, `PRC0`, `PRC3`, `PREC`, `proc`, `PSAP`, `pslt`, `ptbl`, `PTCH`,
  `ptch`, `pthg`, `qrsc`, `qtcm`, `res!`, `RECT`, `resf`, `RMAP`, `ROv#`,
  `ROvr`, `RSSC`, `rtt#`, `RVEW`, `s8mk`, `sb24`, `SB24`, `sbtp`, `scal`,
  `scod`, `scrn`, `sect`, `seg!`, `SERD`, `sfnt`, `sfvr`, `shal`, `SICN`,
  `sift`, `SIGN`, `SIZE`, `slct`, `slut`, `SMOD`, `SMSD`, `snd `, `snth`,
  `SONG`, `SOUN`, `STR `, `STR#`, `styl`, `t8mk`, `tdig`, `TEXT`, `thn#`,
  `TMPL`, `TOC `, `tokn`, `TOOL`, `Tune`, `TwCS`, `TxSt`, `vdig`, `vers`,
  `wart`, `wctb`, `WDEF`, `WIND`, `wstr`, `XCMD`, `XFCN`, `Ysnd`.

## Phase 5: Dependency Integration Cleanup

### Status

- In progress.
- Architecture correction: `rsrcd` is StackImport-owned support code that should
  become more central to typed, memory-safe resource parsing. Do not wrap it in
  pointer/size shims as if it were an opaque third-party vendor. Prefer improving
  and reusing its typed views, parsers, and bounded byte utilities across the
  importer.
- StackImport-owned headers now include `rsrcd.hpp` through the `vendor_rsrcd`
  target include path instead of spelling the filesystem path
  `vendor/rsrcd/include/rsrcd.hpp`; `vendor_rsrcd` is a public usage
  requirement for StackImport targets that expose typed resource APIs.
- MACE decoding now goes through `StackImportMaceResourceDasmAdapter`; the
  private `resource_dasm/src/AudioCodecs.hh` include is isolated to the narrow
  adapter source under `vendor/`.
- PNG writing now goes through `StackImportPngWriter`; `stb_image_write.h` and
  `STB_IMAGE_WRITE_IMPLEMENTATION` are isolated to that adapter source.
- 68K/PowerPC code-resource and ROM disassembly now go through
  `StackImportResourceDasmDisassemblyAdapter`; resource_dasm emulator and
  trap-info headers are isolated to the adapter source under `vendor/`.
- WAV output is currently generated by owned code in
  `StackImportSoundConverter`; unused `vendor_dr_wav` linkage was removed from
  core targets.
- ROM SHA-256 hashing now goes through `StackImportPhosgHashAdapter`; the
  direct `phosg/Hash.hh` include is isolated to an adapter source under
  `vendor/`.
- Vendor tools are required project infrastructure; unused vendored dependencies
  should be removed rather than compiled out.
- Remaining work: fold `resource_dasm` concepts into typed, memory-safe,
  streaming StackImport/rsrcd interfaces where useful; broader
  license/provenance audit.

### Tasks

- Done: stop including private vendor paths such as
  `vendor/resource_dasm/src/AudioCodecs.hh` from StackImport-owned sources.
- Create narrow adapter targets for:
  - Not applicable: resource fork parsing and small resource decoders from
    `rsrcd` should remain typed core support, not an opaque adapter boundary;
  - Done: MACE decoding from `resource_dasm` or an owned codec extraction;
  - Done: disassembly support from `resource_dasm`;
  - Done: PNG encoding through stb;
  - Done: WAV writing through owned code or `dr_wav`.
- Decide whether `vendor_snd2wav` is still needed. If not used by the core
  importer, remove it from default link targets and document it as legacy
  reference material or delete it in a separate cleanup.
- Move vendor-specific compiler suppressions to vendor adapter targets.
- Update `vendor/INDEX.md` for any dependency role changes.

### Tests

- Done: enforce vendor tools as part of the normal build.
- Done: build shared and static targets with warning-as-error settings.
- Add adapter-level tests around MACE decode, PNG bytes, and disassembly fallback
  behavior.

### Lateral Audit

- Check license/provenance notes for any copied codec code.
- Check Homebrew/release build behavior with vendor tools enabled.
- Check that installed headers do not require private vendor include paths unless
  explicitly part of the public API.

## Phase 6: Public Target And Install Hygiene

### Status

- In progress.
- Vendor libraries are linked privately by the core targets, and installation
  still publishes only `stackimport_c.h`.
- Target include directories now distinguish build-tree and install-tree include
  interfaces instead of exporting the source root as the install interface.
- The existing install-tree `pkg-config` smoke test passes in default and
  warning-as-error builds.
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
