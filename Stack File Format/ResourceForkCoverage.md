# Resource Fork Coverage

This file tracks where Classic Mac resource types are currently supported and
where new support should live. The target architecture is:

- `rsrcd` / StackImport core for bounded, typed, memory-safe parsers and simple
  transforms.
- Narrow StackImport adapters for heavy or dependency-rich decoders.
- Raw preservation plus metadata for everything else until a typed parser exists.

## StackImport Core Coverage

| Type | Current support | Home | Notes |
| --- | --- | --- | --- |
| `ICON` | RGBA transform and PNG package artifact | `rsrcd::img`, `StackImportResourceTransforms.cpp` | 32x32 1-bit icon. |
| `ICN#` | RGBA transform and PNG package artifact | `rsrcd::img`, `StackImportResourceTransforms.cpp` | 32x32 1-bit icon with mask. |
| `CURS` | RGBA transform and PNG package artifact | `rsrcd::img`, `StackImportResourceTransforms.cpp` | Includes hotspot metadata. |
| `PAT ` | RGBA transform and PNG package artifact | `rsrcd::img`, `StackImportResourceTransforms.cpp` | Single 8x8 1-bit pattern. |
| `PAT#` | RGBA transform per pattern and PNG package artifacts | `rsrcd::patlist`, `StackImportResourceTransforms.cpp` | 8x8 1-bit patterns. |
| `SICN` | RGBA transform per small icon and PNG package artifacts | `rsrcd::img`, `StackImportResourceTransforms.cpp` | 16x16 1-bit small icon list. |
| `icm#` / `ics#` | RGBA transform and PNG package artifact | `rsrcd::img`, `StackImportResourceTransforms.cpp` | 1-bit mini/small icon lists. Exactly two images are decoded as bitmap plus mask; other counts emit per-image variants. |
| `icl4` / `icl8` / `icm4` / `icm8` / `ics4` / `ics8` | RGBA transform and PNG package artifact | `rsrcd::img`, `StackImportResourceTransforms.cpp` | Opaque 4-bit and 8-bit color icon bitmaps. Parallel mask composition remains future event-stream work. |
| `PLTE` | Typed parse to JSON transform and package artifact | `rsrcd::plte`, `StackImportResourceTransforms.cpp` | Palette window metadata and buttons. |
| `clut` / `CTBL` / `actb` / `cctb` / `dctb` / `fctb` / `wctb` | Typed color-table parse to JSON transform and package artifact | `rsrcd::color_table`, `StackImportResourceTransforms.cpp` | Color table entries with 16-bit RGB. |
| `pltt` | Typed palette parse to JSON transform and package artifact | `rsrcd::pltt`, `StackImportResourceTransforms.cpp` | Palette entries with 16-bit RGB. |
| `HCbg` | Typed AddColor parse to JSON transform and package artifact | `rsrcd::ac`, `StackImportResourceTransforms.cpp` | Background overlay metadata. |
| `HCcd` | Typed AddColor parse to JSON transform and package artifact | `rsrcd::ac`, `StackImportResourceTransforms.cpp` | Card overlay metadata. |
| `STR ` | Typed MacRoman decode to UTF-8 text transform and package artifact | `rsrcd::text`, `StackImportResourceTransforms.cpp` | Pascal string resource. |
| `STR#` | Typed MacRoman string-list decode to JSON transform and package artifact | `rsrcd::text`, `StackImportResourceTransforms.cpp` | Preserves string boundaries. |
| `TEXT` | MacRoman decode to UTF-8 text transform and package artifact | `rsrcd::text`, `StackImportResourceTransforms.cpp` | Raw text resource bytes. |
| `TwCS` | Typed MacRoman string-list decode to JSON transform and package artifact | `rsrcd::text`, `StackImportResourceTransforms.cpp` | Unencrypted strings are decoded; encrypted strings are preserved raw with a parse diagnostic. |
| `TxSt` | Typed text style metadata parse to JSON transform and package artifact | `rsrcd::txst`, `StackImportResourceTransforms.cpp` | Font style, size, RGB color, and font name. |
| `vers` | Typed metadata parse to JSON transform and package artifact | `rsrcd::vers`, `StackImportResourceTransforms.cpp` | Preserves raw numeric version fields and decodes version strings. |
| `SIZE` | Typed metadata parse to JSON transform and package artifact | `rsrcd::size_resource`, `StackImportResourceTransforms.cpp` | Application flags plus preferred/minimum memory sizes. |
| `cfrg` | Typed code-fragment metadata parse to JSON transform and package artifact | `rsrcd::cfrg`, `StackImportResourceTransforms.cpp` | CFM fragment entries, usage/location, names, and extension bytes. |
| `ROv#` | Typed ROM override metadata parse to JSON transform and package artifact | `rsrcd::rov`, `StackImportResourceTransforms.cpp` | ROM version plus overridden resource type/id entries. |
| `RSSC` | Typed code-resource metadata parse to JSON transform and package artifact | `rsrcd::rssc`, `StackImportResourceTransforms.cpp` | RSSC signature, export offsets, and code size. Disassembly remains adapter/code-resource work. |
| `CODE` | Typed code-resource metadata parse to JSON transform and package artifact | `rsrcd::code_resource`, `StackImportResourceTransforms.cpp` | CODE 0 jump table and near/far segment headers. Disassembly remains adapter/code-resource work. |
| `finf` | Typed font metadata parse to JSON transform and package artifact | `rsrcd::finf`, `StackImportResourceTransforms.cpp` | Font id, style flags, and size triples. |
| `CNTL` | Typed UI metadata parse to JSON transform and package artifact | `rsrcd::ui`, `StackImportResourceTransforms.cpp` | Control bounds, state, proc id, refcon, and title. |
| `DLOG` | Typed UI metadata parse to JSON transform and package artifact | `rsrcd::ui`, `StackImportResourceTransforms.cpp` | Dialog bounds, visibility, item list id, title, and auto-position. |
| `WIND` | Typed UI metadata parse to JSON transform and package artifact | `rsrcd::ui`, `StackImportResourceTransforms.cpp` | Window bounds, visibility, title, and auto-position. |
| `MENU` | Typed UI metadata parse to JSON transform and package artifact | `rsrcd::ui`, `StackImportResourceTransforms.cpp` | Menu title, enabled flags, and menu items. |
| `DITL` | Typed UI metadata parse to JSON transform and package artifact | `rsrcd::ui`, `StackImportResourceTransforms.cpp` | Dialog item records, bounds, kind, text info, and resource IDs. |
| `MBAR` | Typed UI metadata parse to JSON transform and package artifact | `rsrcd::mbar`, `StackImportResourceTransforms.cpp` | Counted list of menu resource IDs. |
| `ALRT` / `FREF` / `BNDL` | Typed Finder/UI metadata parse to JSON transform and package artifact | `rsrcd::finder`, `StackImportResourceTransforms.cpp` | Alert bounds/stages, file references, and bundle type/id mappings. |
| `RECT` / `TOOL` / `PICK` / `KBDN` / `PAPA` / `LAYO` | Typed simple metadata parse to JSON transform and package artifact | `rsrcd::simple_metadata`, `StackImportResourceTransforms.cpp` | Rectangle resources, cursor-tool palette layouts, picker resource lists, keyboard names, printer parameters, and layout rectangles. |
| `PICT` | PNG transform and package artifact | `StackImportResourceDasmPictAdapter` | Adapter-backed through resource_dasm; native bytes are still preserved when rendering fails. |
| `snd ` | WAV transform and package artifact | `StackImportSoundConverter.cpp` | MACE uses the resource_dasm-backed MACE adapter when enabled. |
| `XCMD` / `XFCN` | Text disassembly transform and package artifact | `Mac68kDisassembly.cpp` | 68K code resources. |
| `xcmd` / `xfcn` | Text disassembly transform and package artifact | `Mac68kDisassembly.cpp` | PowerPC code resources. |

## resource_dasm Support To Fold Or Adapt

These resource types already have local `resource_dasm` support. For simple,
bounded formats, prefer extracting the parsing model into rsrcd-style APIs. For
large or dependency-rich decoders, keep a narrow adapter until the safe core
interface is clear.

| Family | Types | Current resource_dasm support | Preferred StackImport direction |
| --- | --- | --- | --- |
| PICT | `PICT` | `decode_PICT` and exporter support | Adapter first; current StackImport support uses `StackImportResourceDasmPictAdapter` and does not expose resource_dasm internals to core code. |
| Icon families | `cicn`, `icns` | typed decoders and exporters | Fold small bitmap/icon decoders into rsrcd; keep `icns` container handling adapter-backed until bounded. `ICN#`, `SICN`, `icm#`, `ics#`, `icl4`, `icl8`, `icm4`, `icm8`, `ics4`, and `ics8` now live in StackImport core. |
| Color tables and patterns | `ppat`, `ppt#` | typed decoders and exporters | Fold bounded parsers into rsrcd. `PAT `, `clut`, `CTBL`, `actb`, `cctb`, `dctb`, `fctb`, `wctb`, and `pltt` now live in StackImport core. |
| Text | `styl`, `KCHR` | typed decoders and exporters | Fold MacRoman-aware parsers into rsrcd. `STR `, `STR#`, `TEXT`, `TwCS`, and `TxSt` now live in StackImport core. |
| Metadata/templates | `TMPL` | typed decoders and exporters | Fold small metadata parsers into rsrcd; keep executable/container metadata adapter-backed where complex. `vers`, `SIZE`, `cfrg`, `ROv#`, and `RSSC` metadata now live in StackImport core. |
| UI/layout | None currently listed | typed decoders and exporters | Fold fixed-record parsers into rsrcd after corpus validation. `CNTL`, `DLOG`, `WIND`, `MENU`, `DITL`, `MBAR`, `ALRT`, `FREF`, and `BNDL` now live in StackImport core. |
| Fonts | `FONT`, `NFNT`, `sfnt` | bitmap font decoders and extension mapping | Adapter first for rendered output; fold metadata/bounds checks into rsrcd. `finf` now lives in StackImport core. |
| Code | `DRVR`, `dcmp`, `CDEF`, `INIT`, `LDEF`, `MDEF`, `PACK`, `WDEF`, `FKEY` | 68K/PEF/code metadata decoders and exporters | Keep disassembly/decompiler work behind adapters; fold headers/metadata only. `CODE` metadata now lives in StackImport core. |
| Audio/music | `csnd`, `esnd`, `ESnd`, `Ysnd`, `SMSD`, `SOUN`, `SONG`, `INST`, `Tune`, MIDI-like resources | audio/music decoders and exporters | Adapter first for codecs; fold metadata-only parsing into rsrcd. |
| Template-backed metadata | Other system templates | system-template description | Fold small bounded templates into rsrcd when they appear in corpus output paths. `RECT`, `TOOL`, `PICK`, `KBDN`, `PAPA`, and `LAYO` now live in StackImport core. |

## Broad Fallbacks

Deark remains useful for legacy image/container classification and fallback
conversion, especially around Mac archives and image formats. It should not be a
core resource parser dependency; use it as an optional adapter or comparison
tool when rsrcd/resource_dasm coverage is absent or uncertain.

Unknown resource types should always be preserved as native payloads with type,
id, name bytes, flags, order, size, and hashes where available. Add typed
coverage only when the parser can report bounds errors and preserve uncertain
bytes without guessing.
