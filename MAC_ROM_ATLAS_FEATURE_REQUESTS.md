# Stackimport Feature Requests for Mac ROM Atlas

This document defines requested `stackimport` capabilities for using it as the native analysis backend for `mac-rom-atlas`.

The goal is not only better disassembly. The goal is a continuously improving map of each ROM: what is present, where it lives, how it relates to SuperMario source, and what appears to be missing from the available source snapshot.

## Integration Principles

- `stackimport` owns binary-heavy work: ROM scanning, disassembly, resource parsing, xref extraction, control-flow analysis, and converted assets.
- `mac-rom-atlas` owns orchestration, MCP/REST APIs, browser workflows, notes, and long-lived atlas publication.
- All outputs must be deterministic for the same input bytes, CLI options, and tool version.
- Every generated claim must carry provenance, confidence, and enough evidence to be reviewed.
- Raw ROM bytes, full memory pages, and full disassembly listings remain outside Git by default.
- Atlas files are the stable interchange surface. JSON can be richer, but TSV/Markdown/YAML files are the diffable public contract.

## Common CLI Contract

All ROM-focused features should compose under the existing ROM mode:

```sh
stackimport \
  --rom /path/to/input.ROM \
  --rom-base 0x40800000 \
  --output data/disassembly/<dataset>/<rom-slug> \
  --atlas-output atlas/maps/<dataset> \
  --source-root data/sources/SuperMarioProj.1994-02-09 \
  --emit-atlas \
  --emit-json \
  --emit-assets
```

Required behavior:

- Exit `0` only when all requested outputs were written and validated.
- Exit non-zero when input bytes cannot be read, output files cannot be written, or a requested analysis phase fails.
- Print human logs to stdout/stderr, but never require log parsing for machine data.
- Use repo-relative paths in emitted machine data when paths are under the current working tree.
- Emit unsigned uppercase 8-digit CRC32 and uppercase SHA256.
- Include `tool.name`, `tool.version`, `tool.commit`, `input.path`, `input.size`, `input.crc32`, `input.sha256`, `base_address`, and `generated_at` in `analysis.json`.

## Output File Contract

When `--emit-atlas` is set, write or update these files. If a file has no rows, still write the header.

| File | Purpose |
|---|---|
| `roms.tsv` | ROM identity, stable path, hashes, size, base address. |
| `inventory.tsv` | Per-ROM mapped inventory counts and current SuperMario match status. |
| `regions.tsv` | Broad address map of code, data, tables, resources, and mixed regions. |
| `functions.tsv` | Confirmed and candidate function starts/ranges. |
| `xrefs.tsv` | Address-level code, data, branch, call, trap, and table references. |
| `pointer-tables.tsv` | Pointer table ranges and decoded target counts. |
| `data-regions.tsv` | Strings, tables, packed data, resource forks, jump tables, and unknown data islands. |
| `resources.tsv` | Resource map entries, markers, assets, converted outputs, and decode status. |
| `labels.tsv` | Address labels suitable for IDA-like overlay display. |
| `strings.tsv` | ASCII/Pascal strings with ROM addresses and source hints. |
| `traps.tsv` | A-line trap calls/sites with decoded trap names when known. |
| `source-overlays.tsv` | ROM-to-SuperMario and function-to-source correlation evidence. |
| `source-gaps.tsv` | High-priority regions/functions/resources with no confirmed source coverage. |

All address columns must be uppercase 8-digit hex without `0x`.

## FR-001: Deterministic ROM Identity and Header Analysis

Priority: P0

Add a stable ROM identity pass that runs before any deeper analysis.

Requirements:

- Compute CRC32 as unsigned 32-bit uppercase hex.
- Compute SHA256 uppercase hex.
- Record file size, base address, input path, inferred model/date/checksum tokens, and possible machine family.
- Parse known Old World ROM header fields when present.
- Report whether the first bytes are header/data/code/mixed as a hypothesis with confidence.

Outputs:

- `analysis.json.identity`
- `roms.tsv`
- `inventory.tsv`
- `regions.tsv` header/data rows when recognizable

Acceptance:

- PowerBook 190/190cs ROM emits CRC32 `9C71C823`, not a signed value.
- Quadra 900 reference ROM emits CRC32 `88EA2081`, not a signed value.
- Repeated runs produce byte-identical TSV rows except `generated_at` in JSON.

## FR-002: Real 68k Disassembly Backend

Priority: P0

Enable real 68k disassembly by default when vendored `resource_dasm` is available.

Requirements:

- Build `STACKIMPORT_HAS_RESOURCE_DASM` in the normal CMake path when dependencies are available.
- Fallback `dc.w` output must be explicitly marked as `disassembly_mode=fallback_words`.
- Real disassembly rows must include address, opcode bytes, mnemonic, operands, and optional comment.
- Preserve linear disassembly warnings: tables/data can decode as plausible instructions.
- Emit trap mnemonics or comments for A-line instructions where possible.

Outputs:

- `disassembly.s`
- `analysis.json.disassembly`
- `functions.tsv`
- `xrefs.tsv`
- `traps.tsv`

Acceptance:

- `disassembly.s` for a known 2 MB ROM contains real mnemonics such as `MOVE`, `JSR`, `BRA`, not only `dc.w`.
- `analysis.json.disassembly.mode` is `m68k` when real disassembly is active.
- `mac-rom-atlas` can browse the output without reparsing logs.

## FR-003: Xref and Control-Flow Extraction

Priority: P0

Emit machine-readable references and control-flow facts.

Requirements:

- Decode direct branch, jump, call, trap, and absolute memory references.
- Distinguish `call`, `jump`, `branch`, `return`, `trap`, `memory`, `table`, and `fallthrough`.
- Include source address, target address, mnemonic, line/index, confidence, and evidence.
- Group xrefs by function candidate where known.
- Mark references that point into likely data/resource/table regions.

Outputs:

- `analysis.json.xrefs`
- `xrefs.tsv`
- `functions.tsv` reference/call/jump counts

TSV columns:

```text
id	from	to	kind	source_node	target_node	line	confidence	source
```

Acceptance:

- High-fan-in targets such as current `40944A20` get stable inbound call counts.
- The same ROM analyzed twice emits the same xref IDs and ordering.
- Invalid targets outside ROM address space are emitted only with `confidence=low` or excluded with a warning count.

## FR-004: Function Boundary Hypotheses

Priority: P0

Produce better function candidates while clearly labeling them as hypotheses.

Requirements:

- Use labels, inbound calls, prologues, returns, branch targets, trap setup patterns, and table references.
- Emit start address, optional end address, instruction count, inbound/outbound counts, confidence, and evidence summary.
- Do not overclaim exact boundaries when data islands or jump tables interrupt code.
- Support reanalysis that preserves manually confirmed names supplied via label overlays.

Outputs:

- `analysis.json.functions`
- `functions.tsv`
- `labels.tsv`
- `source-gaps.tsv` for high-priority unnamed candidates

Acceptance:

- Candidate rows include `confidence` and `evidence`.
- Confirmed and candidate functions are distinguishable by `kind` or ID prefix.
- Function IDs remain stable across runs unless the start address changes.

## FR-005: Code/Data/Table Region Classification

Priority: P0

Classify ROM address ranges so linear disassembly can be overlaid with data islands.

Requirements:

- Identify string clusters, pointer tables, jump tables, resource maps, packed data, headers, checksums, and unknown data.
- Emit `mixed` when a range likely contains both executable code and embedded data.
- Include start, end, kind, item count, confidence, and evidence.
- Avoid overlapping high-confidence regions unless one is nested and explicitly marked.

Outputs:

- `regions.tsv`
- `data-regions.tsv`
- `pointer-tables.tsv`
- `analysis.json.regions`

Acceptance:

- The dashboard can color rows as code/data/table/resource without server-side heuristics.
- Known pointer table candidates include decoded target lists in JSON and counts in TSV.

## FR-006: Classic Mac Resource Map and Asset Extraction

Priority: P0

Use stackimport/resource conversion code to identify and extract converted assets from ROM-resident resources.

Requirements:

- Detect resource maps, resource type lists, resource records, names, IDs, attributes, data offsets, and lengths.
- Preserve raw resource bytes under output when `--emit-assets` is set.
- Convert supported visual/audio/text resources where available: `CURS`, `crsr`, `ICON`, `ICN#`, `cicn`, `PICT`, `ppat`, `PAT#`, `snd `, `STR `, `STR#`, `MENU`, `ALRT`, `DITL`, `FOND`, `NFNT`, `FONT`, `DRVR`, `CODE`, `PACK`, `cfrg`, `decl`, `boot`, `ptch`.
- For unsupported resources, emit metadata with `decode_status=preserved`.
- Include output path, media type, dimensions/duration where known, and converter used.

Outputs:

- `resources.tsv`
- `data-regions.tsv`
- converted files under `assets/`
- `analysis.json.resources`

Suggested `resources.tsv` columns:

```text
id	address	kind	resource_type	resource_id	name	media_type	output_file	confidence	source
```

Acceptance:

- Resource rows distinguish marker-only hits from parsed resource records.
- Converted asset files are referenced by repo-relative paths.
- Decode failures are visible as rows, not silent omissions.

## FR-007: String Inventory and Source String Correlation

Priority: P1

Emit a durable string inventory and compare it with SuperMario source text.

Requirements:

- Scan ASCII C strings and Pascal strings.
- Record address, kind, value, length, containing region, and ROM ID.
- De-duplicate identical strings while preserving every address occurrence.
- If `--source-root` is supplied, match strings against source files with path/line/evidence.
- Weight matches by specificity: long unique strings score higher than generic UI fragments.

Outputs:

- `strings.tsv`
- `source-overlays.tsv`
- `inventory.tsv` source overlap fields
- `analysis.json.source_string_matches`

Acceptance:

- Candidate ROM string-overlap scores are reproducible.
- Generic strings can be filtered or down-weighted.
- The PowerBook/Quadra candidate scores can be recomputed without using notes as source data.

## FR-008: SuperMario Function and Symbol Overlay

Priority: P1

Correlate ROM functions/resources with SuperMario source symbols.

Requirements:

- Index source files under `--source-root` without committing source contents.
- Extract candidate symbols from C, Pascal, Rez, and 68k assembly files.
- Match ROM labels/functions/resources to source symbols via strings, resource IDs, trap neighborhoods, and optional byte signatures.
- Emit status values: `confirmed`, `candidate`, `hypothesis`, `rejected`, `missing_from_source`.
- Include source path, line, symbol, score, and evidence.

Outputs:

- `source-overlays.tsv`
- `source-gaps.tsv`
- `labels.tsv`
- `analysis.json.source_overlays`

Acceptance:

- Manual names such as startup/trap setup can be represented as provisional overlays.
- High-priority unmapped function candidates appear in `source-gaps.tsv`.
- A later run can promote an overlay from `candidate` to `confirmed` without changing the target ID.

## FR-009: Missing-from-Source Analysis

Priority: P1

Identify ROM inventory that does not appear to map to the available SuperMario source snapshot.

Requirements:

- Compare mapped ROM regions to source overlays.
- Emit source gaps for important functions, resources, strings, traps, and data regions.
- Include why the gap matters: high fan-in, boot path, resource type, machine-specific marker, unique string, or unavailable source category.
- Distinguish "not yet analyzed" from "probably absent from source."

Outputs:

- `source-gaps.tsv`
- `inventory.tsv`
- `analysis.json.source_gaps`

Acceptance:

- `source-gaps.tsv` is sorted by priority and address.
- Each gap has `gap_kind`, `evidence`, `priority`, and `status`.
- The dashboard can show "what should we investigate next?" directly from the file.

## FR-010: Atlas Export Mode

Priority: P1

Let stackimport write atlas-compatible files directly.

Requirements:

- Add `--emit-atlas` and `--atlas-output <dir>`.
- Write headers even when there are no rows.
- Preserve existing analyst-authored rows if explicitly requested with `--merge-atlas`; otherwise write to a fresh output directory.
- Sort rows by address, then stable ID.
- Validate all TSV files before exit.

Outputs:

- All atlas TSVs listed in the output contract.
- `manifest.yaml`

Acceptance:

- `mac-rom-atlas/scripts/validate_project_data.js` accepts stackimport-emitted maps without post-processing.
- Repeated stackimport runs do not cause noisy diffs.

## FR-011: Analysis JSON Schema

Priority: P1

Stabilize a richer `analysis.json` contract for server import.

Top-level shape:

```json
{
  "schema_version": 1,
  "tool": {
    "name": "stackimport",
    "version": "0.0.0",
    "commit": "unknown"
  },
  "input": {
    "path": "data/roms/old-world/2mb/example.ROM",
    "size": 2097152,
    "crc32": "9C71C823",
    "sha256": "..."
  },
  "base_address": "40800000",
  "identity": {},
  "regions": [],
  "functions": [],
  "xrefs": [],
  "resources": [],
  "strings": [],
  "source_overlays": [],
  "source_gaps": [],
  "warnings": []
}
```

Requirements:

- Keep schema versioned.
- Use strings for hex addresses.
- Avoid absolute paths when an equivalent repo-relative path exists.
- Include warnings for skipped/failed phases.

Acceptance:

- MCP server can import this JSON without parsing `disassembly.s`.
- Unknown future fields do not break existing import.

## FR-012: Incremental and Selective Analysis

Priority: P2

Support focused reanalysis for fast iteration.

Requirements:

- Add `--range <start:end>` to analyze a bounded ROM range.
- Add `--phase identity,strings,disasm,xrefs,resources,source` or equivalent.
- Add `--max-rows`/`--samples` only for debug output; full atlas exports should not silently sample unless requested.
- Cache source index and ROM scan data when safe.

Acceptance:

- A single function/resource neighborhood can be reanalyzed quickly.
- Partial runs mark outputs as partial in JSON/manifest metadata.

## FR-013: Test Corpus and Golden Outputs

Priority: P1

Add regression tests for ROM analysis outputs.

Requirements:

- Use small synthetic ROM fixtures committed to stackimport, not copyrighted ROM bytes.
- Test CRC32 unsigned formatting.
- Test 68k disassembly mode selection.
- Test pointer table detection.
- Test resource map parsing on synthetic resource forks.
- Test atlas TSV headers and stable sort order.

Acceptance:

- `ctest` covers all core output contracts without needing private ROMs.
- Real ROM smoke tests can run locally when `STACKIMPORT_ROM_FIXTURES_DIR` is set.

## FR-014: Open-Source Tool Integration

Priority: P2

Use existing reverse-engineering tools through adapters where they materially improve analysis.

Candidates:

- `resource_dasm` for 68k/PPC disassembly, resource parsing, PICT/icon/audio handling.
- Deark for old binary/resource format decoding where useful.
- Existing Classic Mac resource parsers already vendored in stackimport.

Requirements:

- Keep tool usage behind stackimport adapters.
- Record converter/disassembler name and version/commit in output provenance.
- Do not require `mac-rom-atlas` to know vendor-specific output formats.

Acceptance:

- Swapping or upgrading a vendor library changes stackimport internals, not atlas file schemas.

## Current ROM Scan Evaluation

Observed run:

```text
/tmp/stackimport-rom-scan-20260528-115621
```

Inputs:

| ROM | Size | CRC32 | Exit | Atlas TSVs |
|---|---:|---|---:|---|
| `1994-05 - B6909089 - PowerBook 520 520c 540 540c.ROM` | 2097152 | `66CF9F2F` | 0 | written |
| `1994-09 - 5BF10FD1 - Quadra 660av & 840av.ROM` | 2097152 | `6973886C` | 0 | written |
| `1995-08 - 4D27039C - Powerbook 190 & 190cs.ROM` | 2097152 | `9C71C823` | 0 | written |
| `Quadra-900.rom` | 1048576 | `88EA2081` | 0 | written |

What works now:

- ROM mode exits `0` for all four manifest-verified inputs.
- `analysis.json`, `disassembly.s`, and every expected atlas TSV file are emitted.
- CRC32 and SHA256 values are uppercase and match the local fixture manifest.
- `analysis.json.disassembly.mode` reports `m68k`, and `disassembly.s` contains real mnemonics rather than fallback-only `dc.w` rows.
- `xrefs.tsv`, `traps.tsv`, `functions.tsv`, `labels.tsv`, `strings.tsv`, and region files are populated enough for first-pass atlas browsing.
- Empty source correlation files still include headers, which is good for stable ingestion.

Important gaps found:

- Output paths are absolute in `analysis.json.input.path`, `analysis.json.identity.path`, `analysis.json.output_path`, and `roms.tsv`. Atlas publication needs repo-relative paths when possible.
- Atlas output does not include `manifest.yaml`, despite FR-010 requiring it.
- `resources.tsv` rows are marker-only. Inventory reports `resource_maps=0` and `resource_records=0` for all four ROMs, even though marker scans find `DRVR`, `CODE`, `PACK`, `boot`, `ptch`, `decl`, `MENU`, `DITL`, `ALRT`, `STR `, and `STR#` candidates.
- No assets directory or converted resource payloads are emitted by this run, so FR-006 remains mostly unimplemented beyond marker discovery.
- `source-overlays.tsv` and `source-gaps.tsv` contain only headers. `inventory.tsv` reports `source_status=unmatched`, but no machine-readable "what to investigate next" rows are produced.
- Region classification is still too coarse: every ROM has one high-confidence whole-ROM code region, while `data-regions.tsv` only reports string clusters. This makes the atlas overpaint data/resource/table islands as code.
- Function candidates are overbroad and under-specified. For example, Quadra 900 emits 7,490 candidates, with sample rows having empty `end` and `instruction_count=0`. These are useful labels, but not yet function boundaries.
- Trap counts are very high for linear disassembly (`10,186` to `18,356` per 1-2 MB ROM). Without data-island suppression or function context, many are probably false positives from decoding non-code bytes.
- String extraction is noisy. Early Quadra 900 strings include short low-confidence fragments such as `.T(h` and `"HBa`; atlas users need filtering, scoring, and better defaults for browseable string views.
- Pointer table detection varies sharply across similar ROMs: 71 entries for PowerBook 520, 2,267 for Quadra 660av/840av, 147 for PowerBook 190, and 122 for Quadra 900. That may be real, but it needs validation evidence and false-positive controls.
- Some TSV schemas are missing useful join columns. `resources.tsv` currently lacks `rom_id`, unlike most other atlas files, which complicates multi-ROM aggregation.
- Warnings are empty even when phases are clearly partial, such as marker-only resources, no source root, no parsed resource maps, and whole-ROM linear code classification.

Recommended updates to this feature backlog:

- Keep FR-001 and FR-002 near done but require repo-relative path emission and deterministic re-run comparison before marking complete.
- Treat FR-003 as partially implemented. Add acceptance that xrefs derived from linear disassembly must carry a region-aware confidence downgrade when the source address is inside probable data.
- Treat FR-004 as not complete until candidates have non-empty ranges or explicitly report `boundary_status=unknown`. A zero instruction count should not appear for a function candidate unless the row is clearly a pure label.
- Raise FR-005 implementation priority within P0. Region quality is now the main blocker for trustworthy functions, traps, strings, and xrefs.
- Treat FR-006 as marker-discovery only. Parsed resource-map support, resource records, and asset extraction are the next concrete improvements.
- Treat FR-007 as partially implemented. Add minimum browse filters such as length threshold, printable ratio, duplicate handling, and confidence bands.
- Treat FR-009 as blocked on source overlays and region quality. In the meantime, emit coarse gap rows for high-fan-in candidates, marker-only resources, and major unknown regions instead of empty files.
- Update FR-010 to require a generated manifest with command, input hashes, stackimport version, atlas schema version, row counts, and validation status.
- Update FR-011 to require phase status objects, not just `warnings`, so partial analyses are visible even when the CLI exits successfully.
- Add a TSV schema consistency requirement: every per-ROM atlas TSV should include `rom_id`, stable row IDs, confidence, and source/provenance where applicable.

Coverage pass implemented after this evaluation:

Validated by:

```text
/tmp/stackimport-rom-gap-coverage-final-20260528-124304
```

- `manifest.yaml` is now emitted with tool version, ROM identity, row counts, phase status, validation status, and warnings.
- `analysis.json.phase_status` and `analysis.json.warnings` now make partial phases explicit, including marker-only resources, partial linear xrefs, partial regions, and missing source overlays.
- `resources.tsv` and `analysis.json.resources` now include `rom_id` for marker and parsed rows.
- `functions.tsv` and `analysis.json.functions` now report `boundary_status=unknown` and leave `instruction_count` empty/null instead of implying a zero-length function.
- `source-gaps.tsv` and `analysis.json.source_gaps` now contain high-priority function candidates, marker-only resource candidates, and an analysis-phase gap when no source overlay is available.
- Whole-ROM disassembly regions are downgraded to `mixed` when scanner data regions exist.
- Xrefs and traps derived from probable data regions are downgraded in confidence and tagged in their `source` string.

Final validation counts:

| ROM | Source-gap rows | Mixed regions | Low-confidence xrefs |
|---|---:|---:|---:|
| `1994-05 - B6909089 - PowerBook 520 520c 540 540c.ROM` | 156 | 2 | 62786 |
| `1994-09 - 5BF10FD1 - Quadra 660av & 840av.ROM` | 201 | 2 | 125063 |
| `1995-08 - 4D27039C - Powerbook 190 & 190cs.ROM` | 201 | 1 | 158599 |
| `Quadra-900.rom` | 146 | 2 | 36945 |

Remaining deep-analysis work:

- Parse real ROM-resident resource maps in the tested ROMs, not just marker occurrences.
- Emit converted assets once parsed resource records are found.
- Infer defensible function ranges and instruction counts.
- Add source-root correlation for SuperMario overlays.
- Improve string and pointer-table false-positive filtering with fixture-backed tests.

## Definition of Done

A feature request is complete when:

- CLI behavior is documented in stackimport.
- CMake builds it in the normal developer path or gracefully reports that the phase is unavailable.
- Unit or fixture tests cover the output contract.
- `analysis.json` and atlas TSVs validate.
- `mac-rom-atlas` can consume the output through existing import/export paths.
- Generated claims include source, evidence, and confidence.
- The change avoids committing raw ROMs, full disassemblies, or generated assets to the public atlas repository.
