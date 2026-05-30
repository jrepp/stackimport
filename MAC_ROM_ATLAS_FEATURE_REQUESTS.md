# Mac ROM Atlas Feature Surface

This document records the ROM-analysis capabilities that `stackimport` exposes
for Mac ROM Atlas and related tooling. It replaces the earlier feature-request
backlog with a stable description of what lives in this repository, what output
contracts downstream tools can rely on, and which analysis areas remain
hypotheses or partial coverage.

The canonical CLI user documentation is `docs/cli/rom.mdx`. This file is the
engineering closeout and status reference for the original Mac ROM Atlas feature
request set.

## Ownership Boundary

- `stackimport` owns binary-heavy work: ROM identity, scanning, disassembly,
  resource parsing, xref extraction, converted assets, and machine-readable
  atlas exports.
- Mac ROM Atlas owns orchestration, browser workflows, notes, MCP/REST APIs,
  publication policy, and long-lived analyst-authored overlays.
- Generated claims must include confidence, source/provenance, or phase status
  so downstream tools can distinguish confirmed data from hypotheses.
- Raw ROM bytes, full memory pages, full disassembly listings, and generated
  assets are not intended for public Git storage by default.

## ROM CLI Contract

ROM mode is available through the `rom` subcommand and the legacy `--rom` flag:

```sh
stackimport rom \
  --rom-base 0x40800000 \
  --output data/disassembly/<dataset>/<rom-slug> \
  --atlas-output atlas/maps/<dataset> \
  --source-root data/sources/SuperMarioProj.1994-02-09 \
  --emit-atlas \
  --emit-json \
  --emit-assets \
  --emit-resource-index \
  /path/to/input.ROM
```

Required behavior for the implemented surface:

- Exit `0` only when requested output files are written successfully.
- Exit non-zero when input bytes cannot be read, output directories or files
  cannot be written, or an enabled output phase fails.
- Print human logs to stdout/stderr, while machine consumers use JSON, TSV, YAML,
  and resource-index files.
- Use current-working-directory-relative paths when an emitted path is under the
  current working tree.
- Emit unsigned uppercase 8-digit CRC32 values and uppercase SHA-256 values.
- Preserve phase status and warnings for partial analyses that intentionally
  still exit successfully.

## Primary Outputs

Every successful ROM run writes:

| Path | Purpose |
|---|---|
| `analysis.json` | Machine-readable ROM identity, counts, regions, functions, xrefs, resources, strings, source overlays, source gaps, warnings, and phase status. |
| `disassembly.s` | Annotated linear 68K disassembly when `resource_dasm` is available, or fallback word output when it is not. |

`analysis.json` includes `schema_version`, `tool`, `input`, `generated_at`,
`base_address`, `identity`, `disassembly`, `phase_status`, `counts`, atlas-like
arrays, and `warnings`.

## Atlas TSV Contract

When `--emit-atlas` or `--atlas-output` is supplied, ROM mode writes these
interchange files. Headers are written even when a phase has no rows.

| File | Purpose |
|---|---|
| `roms.tsv` | ROM identity, stable path, hashes, size, base address, and model tokens. |
| `inventory.tsv` | Per-ROM mapped inventory counts and source status. |
| `regions.tsv` | Broad address map of code, data, table, resource, and mixed regions. |
| `functions.tsv` | Confirmed or candidate function starts/ranges. Current rows are hypotheses. |
| `xrefs.tsv` | Address-level code, data, branch, call, jump, memory, and table references. |
| `pointer-tables.tsv` | Pointer table ranges, decoded target counts, confidence, and evidence. |
| `data-regions.tsv` | String clusters, resource maps, resource data, packed resource regions, and similar non-code islands. |
| `resources.tsv` | Marker and parsed resource rows with type, ID, name, lengths, wrapper format, media type, output path, confidence, and source. |
| `labels.tsv` | Address labels suitable for overlay display. |
| `strings.tsv` | Filtered ASCII/Pascal string candidates with confidence scores. |
| `traps.tsv` | A-line trap sites with decoded trap names when available. |
| `source-overlays.tsv` | High-confidence ROM string matches against `--source-root` source files. |
| `source-gaps.tsv` | Prioritized gaps for unmapped function candidates, marker-only resources, and missing source overlay phases. |
| `manifest.yaml` | Tool version, ROM identity, row counts, validation status, phase status, and warnings. |

Address columns use uppercase eight-digit hexadecimal without `0x`.

## Resource Index

`--emit-resource-index` writes a portable resource lookup tree under
`resource-index/`:

| Path | Purpose |
|---|---|
| `resource-index/index.json` | ROM identity plus normalized parsed resource records. |
| `resource-index/resources/` | Preserved raw resources and converted artifacts referenced by `index.json`. |

Resource index entries are keyed by ROM identity, resource type, resource ID,
and ROM address. Entries record name, addresses, flags, length fields,
`wrapper_format`, source, confidence, raw artifact path, and converted artifact
list. This lets later tools depend on ROM-backed resources such as default
QuickDraw or QuickTime palettes without rediscovering the whole ROM.

## Implemented Analysis Surface

### Identity

Status: implemented.

- Computes CRC32 and SHA-256.
- Records input path, byte size, base address, machine family hypothesis, model
  tokens where available, and first-byte kind hypothesis.
- Emits identity to `analysis.json`, `roms.tsv`, `inventory.tsv`, and
  `manifest.yaml`.
- Validated local fixture CRC32 examples include PowerBook 190/190cs
  `9C71C823` and Quadra 900 `88EA2081`.

### Disassembly

Status: implemented with linear-disassembly caveats.

- Uses vendored `resource_dasm` for real 68K disassembly when available.
- Records `analysis.json.disassembly.mode` as `m68k` or `fallback_words`.
- Emits annotated `disassembly.s`.
- Extracted xrefs and trap rows from linear disassembly are marked with phase
  status and confidence because ROM data islands can decode as plausible code.

### References And Traps

Status: partially implemented.

- Extracts direct branch, jump, call, memory, trap, and table references where
  they can be inferred from disassembly comments or pointer scans.
- Emits stable xref IDs, source/target addresses, mnemonic, kind, line index,
  confidence, and source string.
- Downgrades confidence when a reference originates in or points into probable
  data regions.
- Does not yet construct full control-flow graphs or prove fallthrough/return
  ownership across mixed code/data regions.

### Function Candidates

Status: hypothesis output implemented; exact boundaries remain partial.

- Emits function candidates from inbound calls, jumps, and references.
- Rows include kind, label, inbound/outbound/reference counts, confidence, and
  evidence.
- When adjacent candidate starts and parsed instruction addresses support a
  bounded range, rows use `boundary_status=bounded_by_next_candidate` and include
  provisional `end` and `instruction_count`.
- Otherwise rows use `boundary_status=unknown` and leave range/count fields empty
  or null.

### Region And Table Classification

Status: partial but useful for atlas overlays.

- Emits mixed code/data regions when scanner-derived data overlays exist.
- Emits data regions for string clusters, standard resource forks, resource data,
  ROM inline resources, and ROM `Kurt` resources.
- Emits pointer table rows with decoded target counts, confidence, and evidence
  including unique-target counts.
- Region classification remains conservative. Some unknown islands and jump
  tables still need deeper evidence-driven analysis.

### Strings And Source String Correlation

Status: implemented for filtered string inventory and source string matches.

- Scans ASCII and Pascal strings.
- Drops short punctuation-heavy fragments before export to improve default atlas
  browsing.
- Emits string confidence scores in TSV and JSON.
- When `--source-root` is supplied, scans relevant source file types and emits
  high-confidence ROM string matches to `source-overlays.tsv` and
  `analysis.json.source_overlays` with source path, line, symbol, confidence, and
  evidence.
- Correlation is currently string-only. Function, resource, trap-neighborhood,
  Rez, and byte-signature source overlays remain future work.

### Source Gaps

Status: coarse gap output implemented.

- Emits high-priority unmapped function candidates.
- Emits marker-only resource candidates.
- Emits a phase gap when no source root is supplied or no source overlays match.
- Does not yet distinguish all "not analyzed" cases from "probably absent from
  source" with source-aware confidence.

## Resource Mapping And Conversion Surface

### Resource Mapping

Status: implemented for current known ROM layouts.

- Parses standard Classic Mac resource-fork maps embedded in ROM bytes when a
  full fork header is present.
- Detects ROM-packed `Kurt` resource records from nearby type, ID, flags,
  optional Pascal name, and payload length metadata.
- Detects older inline ROM resource records from their 16-byte link/payload
  prefix, type, ID, flags, optional Pascal name, payload offset, and payload
  length metadata.
- Emits marker-only rows when a resource-looking marker is found but a full
  record cannot be validated.
- Emits `length`, `stored_length`, `expected_length`, and `wrapper_format` for
  parsed resources so packed/wrapped resources can be audited separately.

### Converted Resource Families

Status: broad resource conversion coverage is implemented; unsupported payloads
remain preserved.

Implemented conversion families include:

- Visual/image: `ICON`, 12-byte-wrapped ROM `ICON`, `ICN#`, `CURS`, `crsr`,
  `cicn`, `PAT `, `PAT#`, `ppat`, compact ROM `pixs`, guarded PICT-like larger
  `pixs`, `PICT`, color tables (`clut`, `cctb`, `wctb`), icon-list families, and
  bitmap font strikes where bounds are valid.
- Text/UI metadata: `STR `, `TEXT`, `STR#`, `MENU`, `DITL`, `ALRT`, `MBAR`,
  `FREF`, `BNDL`, `ROv#`, `RSSC`, `TxSt`, `styl`, `KCHR`, `RECT`, `TOOL`,
  `PICK`, `KBDN`, `PAPA`, `LAYO`, `FOND`, and simple raw metadata resources
  such as `decl`.
- Audio: sampled `snd ` resources convert to WAV when the standard sampled-sound
  format is recognized; non-converted `snd ` resources still emit JSON metadata
  with format word, conversion status/error, command metadata where available,
  and leading bytes.
- Code: `CODE`, `DRVR`, `dcmp`, `PACK`, `boot`, `ptch`, `XCMD`, and `XFCN`
  emit disassembly artifacts when the built-in converter supports the payload.

Recent local fixture observations:

- PowerBook 190/190cs and Quadra 660av/840av ROM-packed resources are parsed and
  asset outputs are emitted under `assets/resources/` and `resource-index/`.
- Compact ROM `pixs` records and `pixs` ID `-10208` render to PNG in the
  PowerBook 190/190cs and Quadra 660av/840av validation runs.
- 140-byte wrapped ROM `ICON` records render to PNG.
- Most DiskMode `PICT` resources in the validated ROMs render to PNG.
- `clut` ID 8 (`8BitStd`) and `clut` ID 4 (`4BitStd`) are available as ROM
  default-palette candidates in named Old World ROM runs.

## Known Limitations

The original Mac ROM Atlas backlog is closeable as a feature-request document.
Remaining work should be tracked as ordinary implementation issues or docs
updates:

- Compact/special `cicn` ID `-20020` remains preserved raw until its compact
  layout is documented and fixture-backed.
- Large DiskMode 6 `PICT` variants in the PowerBook 190/190cs and Quadra
  660av/840av ROMs remain preserved raw until their wrapper or opcode coverage is
  understood.
- Function ranges are hypotheses. `bounded_by_next_candidate` does not prove
  exact function exits, jump-table ownership, or embedded data boundaries.
- Source correlation is currently high-confidence string matching only.
  Function/resource/trap/Rez correlation remains future work.
- Region classification is still partial. Unknown data islands, checksums,
  packed tables, and jump tables need evidence-driven refinements.
- `--range`, `--phase`, cache-aware incremental analysis, and `--merge-atlas`
  are not part of the current stable CLI surface.
- Full determinism is defined for the same input bytes, current working
  directory, CLI options, and tool version. `analysis.json.generated_at` is
  intentionally time-varying.

## Validation Workflow

For C++ or ROM-mode changes, run the normal and strict CMake workflows:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSTACKIMPORT_STRICT_WARNINGS=ON \
  -DSTACKIMPORT_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -Wdev --warn-uninitialized
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

For docs changes under `docs/` or `website/`, run from `website/`:

```sh
pnpm lint
pnpm build
```

Private ROM smoke testing uses manifest-verified local fixtures from:

```text
/Users/jrepp/d/hype-import-tests/roms/manifest.tsv
```

Do not commit ROM bytes, full disassemblies, or generated resource assets.

## Closeout Determination

The original Mac ROM Atlas feature-request list has served its purpose. The
implemented repository surface now includes ROM identity, real 68K disassembly,
atlas TSV export, manifest output, phase status/warnings, resource parsing,
resource asset/index emission, broad conversion coverage, string filtering,
source string overlays, coarse source gaps, and fixture-backed regression tests.

This document should be maintained as a stable feature/status reference. New
work should be filed against the specific implementation area rather than added
as another open-ended feature request list.
