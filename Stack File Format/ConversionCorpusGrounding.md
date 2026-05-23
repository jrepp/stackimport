# Conversion Corpus Grounding

This document grounds the conversion corpus for legacy HyperCard stack artifacts.
The goal is to preserve original bytes, emit modern inspection formats, and keep
enough metadata to compare source chunks and embedded files across many stacks.

All conversions should write a corpus row with:

- source kind: stack block type, resource type, or generated stack metadata
- source id/name, byte size, SHA-256, and original artifact path
- target path, target format, target SHA-256, converter name/version
- understood state: `parsed`, `preserved`, `partially_parsed`, or `gap`
- gap detail when parsing/conversion is incomplete

## Vendoring Baseline

Preferred vendorable references:

- `resource_dasm` / `libresource_file`: MIT-licensed Classic Mac resource
  parsing/conversion library. It reads resource forks, AppleSingle/AppleDouble,
  MacBinary, and data-fork resource files, and converts many Classic Mac resource
  types into modern outputs. It also has code disassemblers and resource
  decompression support. See <https://github.com/fuzziqersoftware/resource_dasm>
  and license <https://raw.githubusercontent.com/fuzziqersoftware/resource_dasm/master/LICENSE>.
- Deark: MIT-style licensed old-format decoder with Mac resource, MacBinary,
  BinHex, StuffIt, and Macintosh PICT coverage. This is useful as a smaller
  executable/reference for format comparison. See <https://entropymine.com/deark/>
  and <https://entropymine.com/deark/COPYING>.
- PNG writers: `stb_image_write.h` is public-domain/MIT style, good for direct
  vendoring; `libpng` is mature but heavier and uses the permissive zlib/libpng
  license. See OSI zlib/libpng license text at
  <https://opensource.org/license/zlib>.
- WAV writer: `dr_wav.h` from `dr_libs` is single-header and distributed under
  Unlicense or MIT-0 in downstream packaging; use it for writing PCM WAV if we
  do not vendor all of `resource_dasm` audio output. See
  <https://github.com/mackron/dr_libs>.

Vendoring rule: preserve upstream copyright and license files in `third_party/`
and keep any imported code in an isolated namespace/build target.

## `BMAP` Stack Blocks

Source format:

- HyperCard stack block with type `BMAP`.
- Payload is WOBA-compressed 1-bit bitmap data used by cards/backgrounds.
- The block id becomes the logical bitmap id and current output name
  `BMAP_<id>.pbm`.

Current parser:

- `woba.cpp` decodes WOBA to `picture`.
- `picture.cpp` writes PBM.
- `--rawgraphics` writes raw compressed bytes instead.

Modern target:

- Preserve raw compressed bytes as `.bin` when requested.
- Emit 1-bit PBM for exact current behavior.
- Emit PNG for inspection and downstream visual comparison.
- Optional: emit QOI for fast local corpus diffs, but PNG remains the baseline.

Vendoring candidate:

- Keep local WOBA decoder as project-owned code.
- Vendor `stb_image_write.h` or `libpng` for PNG emission.

Corpus fields:

- `binary_chunks`: `chunk_type=BMAP`, id, compressed byte count, understood flag.
- `embedded_files`: `embedded_kind=image`, `logical_kind=bmap`, source chunk id.
- Additional image metadata: width, height, bit depth, mask presence, decode
  warnings.

## `PAT` Stack Patterns

Source format:

- Stack-level 8x8 1-bit QuickDraw patterns decoded from `STAK` metadata, not
  standalone blocks.
- Current generated names are `PAT_<n>.pbm`.

Current parser:

- `LoadStackBlock` emits one PBM per pattern.

Modern target:

- Preserve generated PBM.
- Emit one PNG per pattern.
- Emit a stack-level contact sheet/atlas PNG for quick comparison.

Vendoring candidate:

- Same PNG writer as `BMAP`.

Corpus fields:

- `embedded_files`: `embedded_kind=image`, `logical_kind=pat`.
- Mark source as `stack_pattern` rather than block-backed.
- Track duplicate hashes across stacks to identify common HyperCard defaults.

## `PICT` Resources

Source format:

- Classic Macintosh QuickDraw picture resource.
- PICT is a stream of QuickDraw drawing opcodes; Apple documentation covers
  PICT files/resources, version 1 and version 2, and opcode tables in Inside
  Macintosh: Imaging With QuickDraw.

Current parser:

- Mac-only Carbon path writes `PICT_<id>.pict` with a synthetic 512-byte header.
- No non-Carbon resource fork reader is currently enabled in the CMake build.

Modern target:

- Preserve original resource bytes as `.pict` or `.bin`.
- Emit PNG for raster inspection.
- Optionally emit PDF/SVG only when a converter can preserve vector semantics.

Vendoring candidate:

- Prefer `resource_dasm` for resource extraction and PICT conversion. Its README
  documents internal PICT decoding, fallback to Netpbm `picttoppm`, and raw PICT
  preservation when conversion fails.
- Deark is a second MIT-style reference decoder for Macintosh PICT.

Corpus fields:

- `embedded_files`: `embedded_kind=image`, `logical_kind=pict`.
- Record whether output was internal decode, Netpbm fallback, or preserved raw.
- Store PICT version/opcode coverage if exposed by the converter.

## `ICON` Resources

Source format:

- Classic `ICON` resource: 32x32 1-bit bitmap, 128 bytes, no explicit mask in
  the `ICON` resource itself.
- Related `ICN#` icon-family resources include bitmap plus mask, but `ICON` is
  what stackimport currently handles.

Current parser:

- Mac-only Carbon path converts `ICON` to PBM and builds a mask from
  surroundings.

Modern target:

- Preserve raw 128-byte icon resource.
- Emit PNG with alpha/mask strategy recorded.
- Optional: emit ICNS only as a packaging convenience, not as the canonical
  target.

Vendoring candidate:

- `resource_dasm` already handles `ICON` and broader icon-family resources.
- Direct local decoder is trivial enough to implement if we want no dependency:
  32 rows, 4 bytes per row, MSB-first pixels.

Corpus fields:

- resource id/name, mask strategy, dimensions `32x32`, bit depth `1`.
- conversion status: `decoded_direct`, `decoded_resource_dasm`, or `preserved`.

## `CURS` Resources

Source format:

- Classic cursor resource with 16x16 1-bit bitmap, 16x16 1-bit mask, then hotspot
  coordinates.
- Current code reads 32 bytes bitmap, 32 bytes mask, then vertical and horizontal
  hotspot words.

Current parser:

- Mac-only Carbon path is not currently wired into the RapidJSON output path.
  The intended portable path writes image data and records hotspot metadata in
  JSON.

Modern target:

- Preserve raw cursor resource.
- Emit PNG with alpha.
- Emit JSON metadata or DB fields for hotspot x/y.

Vendoring candidate:

- `resource_dasm` supports `CURS` and cursor variants.
- Direct local decoder is straightforward.

Corpus fields:

- `embedded_kind=cursor`, hotspot x/y, dimensions `16x16`, bit depth `1`.
- Link cursor media JSON to output file.

## `snd ` Resources

Source format:

- Classic Mac `snd ` sound resource.
- Encodings may include uncompressed PCM and compressed forms such as MACE,
  IMA4, A-law, and mu-law in real-world resources.

Current parser:

- Mac-only Carbon path either uses QuickTime to AIFF or writes `snd_<id>.bin`
  and converts to WAV through local `snd2wav`.
- The old metadata path wrote `snd_<id>.aiff` even in the non-QuickTime path;
  that should be corrected when this path is ported to RapidJSON.

Modern target:

- Always preserve raw `snd ` bytes.
- Emit WAV as canonical modern audio.
- Optionally emit AIFF when the source is directly representable and metadata is
  useful.

Vendoring candidate:

- `resource_dasm` has audio codec support and emits WAV or MP3 for `snd`,
  including common compression forms.
- `dr_wav.h` is suitable for WAV writing if we keep local decode logic.

Corpus fields:

- sample rate, channels, sample width, frame count, duration, codec/compression,
  raw hash, WAV hash, decode status.

## `XCMD` and `XFCN` Resources

Source format:

- 68K HyperCard external command/function code resources.
- They are executable Classic Mac code resources, often named and invoked by
  scripts.

Current parser:

- Mac-only path exports raw `.data`.
- Emits JSON metadata as external command/function with platform `mac68k`.
- Logs warnings because code is preserved, not semantically decoded.

Modern target:

- Preserve raw `.bin`.
- Emit metadata JSON.
- Optionally emit disassembly text for inspection.
- Do not execute.

Vendoring candidate:

- `resource_dasm` / `m68kdasm` can disassemble `XCMD` and `XFCN` resources and
  is MIT-licensed.

Corpus fields:

- `embedded_kind=code_resource`, platform `mac68k`, command/function, id, name,
  byte size, SHA-256, optional strings and disassembly path.

## `xcmd` and `xfcn` Resources

Source format:

- PowerPC HyperCard external command/function code resources.

Current parser:

- Mac-only path exports raw `.data`.
- Emits JSON metadata as platform `macppc`.

Modern target:

- Preserve raw `.bin`.
- Emit metadata JSON.
- Optionally emit PPC disassembly text or PEF/container metadata when present.
- Do not execute.

Vendoring candidate:

- `resource_dasm` / `m68kdasm` includes PPC32 disassembly and Classic Mac code
  resource handling.

Corpus fields:

- Same as 68K code resources, with platform `macppc`.

## `HCbg` and `HCcd` AddColor Resources

Source format:

- AddColor resources attached to background/card ids.
- `HCbg` maps to a background id; `HCcd` maps to a card id.
- Data is big-endian and contains sequential object records. Known object types:
  button color, field color, rectangle, picture resource, and picture file.

Current parser:

- Mac-only path parses object types `0x01` through `0x05`.
- Unknown object types log an error and abort parsing the rest of that resource.

Modern target:

- Preserve raw resource bytes.
- Emit structured JSON overlay model.
- Emit SVG or PNG overlay preview only after object parsing is stable.
- Resolve picture-resource references to converted `PICT` outputs.
- Resolve picture-file references to extracted archive files when possible.

Vendoring candidate:

- Local parser is already based on documented AddColor structure and should
  remain local.
- Use resource-fork extraction from `resource_dasm` to surface `HCbg`/`HCcd`.

Corpus fields:

- overlay target layer (`background` or `card`), target id, object type, hidden
  flag, rect, bevel, RGB16 values, source `resource` or `file`, picture name/path.
- gap row for unknown object type with resource id and byte offset.

## AddColor Picture File References

Source format:

- AddColor picture object type `0x05` stores a MacRoman HFS file/path string.
- The referenced file may be inside the same extracted archive, adjacent to the
  stack, or missing.

Modern target:

- Resolve path against archive extraction root.
- Classify referenced file by Finder type, extension, and magic.
- If it is PICT, convert through PICT path.
- Otherwise preserve and index as external binary.

Vendoring candidate:

- Deark and `resource_dasm` can help classify/decode many legacy containers and
  image formats.

Corpus fields:

- addcolor object id, source stack, reference text, resolved path, resolution
  status, target conversion id.

## Raw `.data`, `.raw`, and Unknown Resources

Source format:

- Any preserved resource or block whose semantics are not fully decoded.
- Includes code resources, raw BMAP fallback, failed PICT/sound conversions, and
  unknown Classic Mac resources surfaced by resource extraction.

Modern target:

- Preserve as `.bin`.
- Emit sidecar JSON metadata.
- Optionally run safe classifiers: magic bytes, entropy, strings, MacRoman text
  probe, 1-bit image probe, and resource template (`TMPL`) decode if available.

Vendoring candidate:

- `resource_dasm` template/resource handling.
- Deark for broad legacy file identification.

Corpus fields:

- `understood=0` until a semantic decoder exists.
- classifier hints, entropy, printable-text ratio, candidate image dimensions,
  and duplicate hashes across stacks.

## Source References

- Local parser and docs: `CStackFile.cpp`, `woba.cpp`, `picture.cpp`, and this
  `Stack File Format/` directory.
- HyperCard AddColor format:
  <https://hypercard.org/addcolor_resource_format/>
- Apple QuickDraw / PICT documentation index:
  <https://developer.apple.com/library/archive/documentation/mac/pdf/Imaging_With_QuickDraw/Imaging_IX.pdf>
- Classic Mac resource conversion reference:
  <https://github.com/fuzziqersoftware/resource_dasm>
- Deark old-format decoder:
  <https://entropymine.com/deark/>
- zlib/libpng license:
  <https://opensource.org/license/zlib>
