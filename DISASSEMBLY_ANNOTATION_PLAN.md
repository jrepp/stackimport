# Disassembly Annotation And Reader Learning Plan

## Goals

- Make generated 68K and PowerPC XCMD/XFCN disassembly easier to read without
  replacing the raw instructions.
- Help readers progress from "what is this resource?" to "what does this
  extension do?" to "where does it call out of its own code?"
- Keep audit data consolidated in `run.db` so corpus-wide questions can be
  answered with SQL instead of scattered report files.

## Annotation Work

1. Use `resource_dasm`'s `TrapInfo` table as the authoritative Classic Mac OS
   and Toolbox trap-name source.
2. Add a StackImport trap taxonomy that maps trap names to broad categories:
   Memory Manager, Resource Manager, File Manager, QuickDraw, Dialog Manager,
   Event Manager, Menu/Control Manager, Sound, QuickTime, Text, Process, and
   Communications.
3. Keep instruction lines intact and add comments only. Use consistent prefixes:
   `intent:`, `call:`, `trap:`, `abi:`, `memory:`, `resource:`, `ui:`, `data:`,
   and `risk:`.
4. Mark extension exits:
   - OS and Toolbox traps.
   - 68K indirect `jsr`/`jmp` through registers or absolute pointers.
   - PPC indirect branch-link forms such as `bctrl`.
   - PPC branch-link targets that point outside decoded code or into imports.
5. Mark internal calls:
   - 68K PC-relative `jsr`/`jmp`/`bsr`.
   - PPC direct branch-link targets that resolve inside decoded code.
6. Add return-site comments for `rts` and `blr`.
7. Detect common handle/pointer lifecycle patterns around `NewHandle`, `HLock`,
   `HUnlock`, `DisposeHandle`, `NewPtr`, `DisposePtr`, `SetHandleSize`, and
   `BlockMove`.
8. Detect XCMD/XFCN ABI access patterns, especially parameter-block loads,
   result writes, callback dispatch, and handle-return conventions.
9. Detect data regions: Pascal strings, C strings, jump tables, selector tables,
   and long invalid-instruction runs.
10. Improve PPC legibility by detecting PEF/cfrg-style headers and avoiding
    treating known headers as executable instructions.

## Reader-Facing Output

1. Add a header to each `.s` file with resource type/id/name, platform, payload
   size/hash, source stack, and a short behavior summary.
2. Add a "reading guide" block near the top: entry point, likely returns,
   external exits, top traps, and notable data regions.
3. Prefer plain-language comments, for example:
   `trap: HLock locks a relocatable handle before pointer access`.
4. Add confidence labels for inferred facts: `high`, `medium`, `low`.
5. Emit sidecar facts as SQLite rows and optionally JSON sidecars only if useful
   for external tools.
6. Add glossary documentation for trap, Toolbox, OS trap, handle, resource fork,
   Pascal string, XCMD, XFCN, cfrg, PEF, QuickDraw, and HyperCard callback.
7. Add example walkthroughs:
   - 68K XCMD UI/QuickDraw helper.
   - XFCN file/resource-access helper.
   - PPC resource with header and indirect exits.

## Audit And Reports

1. Keep detailed call sites in SQLite:
   stack, resource, platform, address, line, call kind, target, category,
   confidence, and source instruction.
2. Keep aggregate usage in SQLite:
   target/category counts, distinct resources, distinct stacks, and payload
   hashes.
3. Add call graph data:
   resource-to-label, resource-to-trap, resource-to-indirect-exit, and
   resource-to-import edges.
4. Add behavior tags per resource:
   `uses_files`, `uses_resources`, `draws_quickdraw`, `opens_dialogs`,
   `uses_menus`, `uses_sound`, `uses_quicktime`, `uses_network_or_comm`,
   `allocates_handles`, `uses_callbacks`.
5. Add known-library fingerprints by payload hash and normalized instruction
   windows for repeated helpers such as AddColor, Lightbox, TrueTools, and POO's
   utilities.

## Validation

1. Test on focused stacks before a full corpus run:
   AddColor-heavy, Lightbox, TrueTools, POO's helpers, file/resource-heavy XFCNs,
   PPC-only resources, and currently missing/odd 68K resources.
2. Track regression metrics:
   disassembly present/missing counts, call-site counts, trap category counts,
   unknown exit counts, invalid PPC region counts, and duplicate library
   fingerprints.
3. Keep raw disassembly stable enough that annotations do not break downstream
   indexing.

## Implementation Order

1. Keep report data in SQLite report tables.
2. Stabilize the call-audit schema and add category/confidence columns.
3. Add trap taxonomy.
4. Add per-resource summaries and behavior tags.
5. Add ABI pattern recognition.
6. Add data-region detection.
7. Improve PPC header/import handling.
8. Add duplicate-library fingerprints.
9. Document report tables and reader workflows.
