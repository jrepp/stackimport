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
| `CURS` | RGBA transform and PNG package artifact | `rsrcd::img`, `StackImportResourceTransforms.cpp` | Includes hotspot metadata. |
| `PAT#` | RGBA transform per pattern and PNG package artifacts | `rsrcd::patlist`, `StackImportResourceTransforms.cpp` | 8x8 1-bit patterns. |
| `PLTE` | Typed parse to JSON transform and package artifact | `rsrcd::plte`, `StackImportResourceTransforms.cpp` | Palette window metadata and buttons. |
| `HCbg` | Typed AddColor parse to JSON transform and package artifact | `rsrcd::ac`, `StackImportResourceTransforms.cpp` | Background overlay metadata. |
| `HCcd` | Typed AddColor parse to JSON transform and package artifact | `rsrcd::ac`, `StackImportResourceTransforms.cpp` | Card overlay metadata. |
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
| Icon families | `ICN#`, `icm#`, `ics#`, `icl4`, `icl8`, `icm4`, `icm8`, `ics4`, `ics8`, `SICN`, `cicn`, `icns` | typed decoders and exporters | Fold small bitmap/icon decoders into rsrcd; keep `icns` container handling adapter-backed until bounded. |
| Color tables and patterns | `PAT `, `ppat`, `ppt#`, `clut`, `CTBL`, `pltt`, `actb`, `cctb`, `dctb`, `fctb`, `wctb` | typed decoders and exporters | Fold bounded parsers into rsrcd. |
| Text | `STR `, `STR#`, `TEXT`, `styl`, `TwCS`, `KCHR` | typed decoders and exporters | Fold MacRoman-aware parsers into rsrcd. |
| Metadata/templates | `TMPL`, `vers`, `SIZE`, `cfrg`, `ROv#`, `RSSC` | typed decoders and exporters | Fold small metadata parsers into rsrcd; keep executable/container metadata adapter-backed where complex. |
| UI/layout | `CNTL`, `DLOG`, `WIND`, `DITL`, `MENU` | typed decoders and exporters; system templates also cover `ALRT`, `BNDL`, `FREF`, `MBAR` | Fold fixed-record parsers into rsrcd after corpus validation. |
| Fonts | `FONT`, `NFNT`, `finf`, `sfnt` | bitmap font decoders and extension mapping | Adapter first for rendered output; fold metadata/bounds checks into rsrcd. |
| Code | `CODE`, `DRVR`, `dcmp`, `CDEF`, `INIT`, `LDEF`, `MDEF`, `PACK`, `WDEF`, `FKEY` | 68K/PEF/code metadata decoders and exporters | Keep disassembly/decompiler work behind adapters; fold headers/metadata only. |
| Audio/music | `csnd`, `esnd`, `ESnd`, `Ysnd`, `SMSD`, `SOUN`, `SONG`, `INST`, `Tune`, MIDI-like resources | audio/music decoders and exporters | Adapter first for codecs; fold metadata-only parsing into rsrcd. |

## Broad Fallbacks

Deark remains useful for legacy image/container classification and fallback
conversion, especially around Mac archives and image formats. It should not be a
core resource parser dependency; use it as an optional adapter or comparison
tool when rsrcd/resource_dasm coverage is absent or uncertain.

Unknown resource types should always be preserved as native payloads with type,
id, name bytes, flags, order, size, and hashes where available. Add typed
coverage only when the parser can report bounds errors and preserve uncertain
bytes without guessing.
