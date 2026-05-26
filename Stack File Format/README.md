# HyperCard Stack File Format Notes

This directory collects reverse-engineered notes for reading HyperCard stack
files. The format was not officially published; these notes are partial and are
intended to support reading existing stacks, not writing new ones.

Primary sources represented here:

- Local `Stack File Format/*/*.txt` notes, copied from the older xtalk code.
- Current parser behavior in `CStackFile.cpp`.
- Creysoft's HyperCard file format write-up:
  <https://creysoft.com/xtalk/hypercard_file_format2.htm>
- Rebecca Bettencourt's 2011 guide, folded in from local file
  `~/Downloads/HC FILE FORMAT 2010.txt`; see `DefinitiveGuide2011.md`.

## File Conventions

- Stack file data is big-endian.
- Text is MacRoman.
- Stack files are streams of blocks terminated by a `TAIL` block.
- Each block uses this outer layout:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 4 | Block size, including the full block header |
| 4 | 4 | Four-character block type |
| 8 | 4 | Signed block ID |
| 12 | 4 | Filler, always zero |
| 16 | n | Type-specific data |

`CStackFile.cpp` reads from offset 12 into `blockData`, so parser offsets include
the filler word as the first four bytes of `blockData`.

Classic Mac resource-fork data is separate from the block stream. The current
parser reads resources through Carbon when `MAC_CODE` is enabled.

## Block Coverage

| Type | Coverage | Notes | Local Reference |
| --- | --- | --- | --- |
| `STAK` | Implemented | Stack metadata, dimensions, patterns, script, pointers to `LIST`, `FTBL`, `STBL`. | `Stack/ReadBlock.txt`, `CStackFile::LoadStackBlock` |
| `STBL` | Implemented | Style table used by styled field text. | `CStackFile::LoadStyleTable` |
| `FTBL` | Implemented | Font ID to font name table. | `FontTable/ReadFTBLBlock.txt`, `CStackFile::LoadFontTable` |
| `LIST` | Implemented | Lists `PAGE` block IDs and page-entry size. | `Stack/ReadBlock.txt`, `CStackFile::LoadListBlock` |
| `PAGE` | Implemented | Ordered card IDs and card flags. | `Stack/ReadBlock.txt`, `CStackFile::LoadPageTable` |
| `BKGD` | Implemented | Background layer data: parts, contents, name, script. | `Background/ReadBKGDBlock.txt`, `CStackFile::LoadLayerBlock` |
| `CARD` | Implemented | Card layer data: owner background, parts, contents, name, script. | `Card/ReadCARDBlock.txt`, `CStackFile::LoadLayerBlock` |
| `BMAP` | Implemented | WOBA bitmap data decoded to PBM, or raw output with `--rawgraphics`. | `woba.cpp`, `picture.cpp` |
| `PRFT` | Implemented | Report template data and report items. | `DefinitiveGuide2011.md`, `ReportTemplate/ReadPRFTBlock.txt`, `ReportTemplate/ReadItemData.txt`, `CStackFile::LoadReportTemplateBlock` |
| `PRST` | Implemented partially | QuickDraw Printing Manager page setup fields with HyperCard block header. | `DefinitiveGuide2011.md`, `Stack/ReadBlock.txt`, `CStackFile::LoadPageSetupBlock` |
| `PRNT` | Implemented partially | Page setup ID and report-template index; some internal fields remain unknown. | `DefinitiveGuide2011.md`, `CStackFile::LoadPrintBlock` |
| `MAST` | Implemented | Master reference object mapping block offsets and low ID bytes. | `DefinitiveGuide2011.md`, `CStackFile::LoadMasterBlock` |
| `FREE` | Skipped | Free/reusable space in the block stream. | `CStackFile::LoadFile` |
| `TAIL` | Documented terminator | Ends the block stream. | `DefinitiveGuide2011.md`, `CStackFile::LoadFile` |

## Resource Coverage

| Type | Coverage | Notes | Local Reference |
| --- | --- | --- | --- |
| `ICON` | Implemented | Converted to PBM. | `CStackFile::LoadBWIcons` |
| `PICT` | Implemented | Exported as PICT data with a synthetic header. | `CStackFile::LoadPictures` |
| `CURS` | Implemented | Converted to PBM. | `CStackFile::LoadCursors` |
| `snd ` | Implemented | Exported raw and converted to WAV through the owned sound converter. Optional MACE support depends on the resource_dasm adapter. | `CStackFile::LoadSounds` |
| `HCbg` | Implemented partially | AddColor data for backgrounds. Known object types are decoded. | `AddColor.md`, `CStackFile::LoadLayerBlock` |
| `HCcd` | Implemented partially | AddColor data for cards. Known object types are decoded. | `AddColor.md`, `CStackFile::LoadLayerBlock` |
| `XCMD` | Exported, not decoded | 68K external command code resource; written as raw `.data` with JSON metadata. | `CStackFile::Load68000Resources` |
| `XFCN` | Exported, not decoded | 68K external function code resource; written as raw `.data` with JSON metadata. | `CStackFile::Load68000Resources` |
| `xcmd` | Exported, not decoded | PowerPC external command code resource; written as raw `.data` with JSON metadata. | `CStackFile::LoadPowerPCResources` |
| `xfcn` | Exported, not decoded | PowerPC external function code resource; written as raw `.data` with JSON metadata. | `CStackFile::LoadPowerPCResources` |

## Implementation Status

The parser now handles every documented stack block type that carries useful
data:

| Status | Types |
| --- | --- |
| Parsed to JSON/media | `STAK`, `MAST`, `LIST`, `PAGE`, `BKGD`, `CARD`, `BMAP`, `STBL`, `FTBL`, `PRNT`, `PRST`, `PRFT` |
| Structural handling only | `FREE`, `TAIL` |
| Resource-fork parsed/exported | `ICON`, `PICT`, `CURS`, `snd `, `HCbg`, `HCcd`, `XCMD`, `XFCN`, `xcmd`, `xfcn` |

Known limits:

- `PRNT` still has unknown internal fields beyond page setup ID and report
  template index.
- `PRST` is emitted as selected QuickDraw page setup fields, not a byte-for-byte
  semantic model of every reserved field.
- External command/function code resources are exported as raw data and JSON
  metadata. They are not decoded or executed.
- AddColor supports documented object types `0x01` through `0x05`; unknown
  object types stop AddColor parsing for that resource.

## Understanding Levels

This table separates format knowledge from implementation coverage. "High" means
the binary structure is documented locally and the parser now follows that
structure in corpus runs. "Medium" means useful fields are decoded but reserved
or application-specific bytes remain. "Low" means data is recognized but not
semantically decoded.

| Area | Understanding | Parser State | Evidence |
| --- | --- | --- | --- |
| Block stream envelope | High | Implemented | Size/type/id/filler model matches the local notes and the Science corpus. |
| `STAK` | High | Implemented | Stack metadata and cross-block IDs parse across Science stacks. |
| `MAST` | Medium | Implemented | Offset/id reference entries are decoded; leading unknown fields are preserved only as raw data when requested. |
| `LIST` | High | Implemented | `LIST` page-entry counts are authoritative for parsing `PAGE` blocks. |
| `PAGE` | High for card IDs and flags; Medium for extra bytes | Implemented | Card IDs and marked flag are decoded; remaining per-card bytes are likely search/index data. |
| `BKGD` / `CARD` | High for layer, part, text structures; Medium for some filler bytes | Implemented | Shared layer parser handles Science stacks; several filler fields remain named as unknown/reserved. |
| `BMAP` / WOBA | Medium | Implemented with bounds clipping | Rectangles and WOBA streams are decoded; row-reference opcodes can refer outside the allocated image on some stacks and are clipped defensively. |
| `STBL` | Medium | Implemented | Style fields and QuickDraw style bits decode; reserved fields remain opaque. |
| `FTBL` | High | Implemented | Font IDs and MacRoman names decode. |
| `PRNT` | Medium | Implemented partially | Page setup ID and report-template index decode; other print settings remain unknown. |
| `PRST` | Medium | Implemented partially | Selected QuickDraw page setup fields decode; not every reserved byte is modeled. |
| `PRFT` | High | Implemented | Report template and item structures decode from local documentation. |
| `FREE` / `TAIL` | High | Structural handling | `FREE` is skipped and `TAIL` terminates the stream. |
| Mac resources `ICON`, `PICT`, `CURS`, `snd ` | Medium | Implemented/exported when resource-fork support is available | Resource content is converted or exported; behavior depends on build platform and resource fork preservation. |
| AddColor `HCbg` / `HCcd` | Medium | Implemented partially | Known object records decode; unknown AddColor object types abort that resource. |
| Code resources `XCMD`, `XFCN`, `xcmd`, `xfcn` | Low | Exported, not decoded | Code/resource metadata is preserved but code is not interpreted. |

## Corpus Findings

The Science subset of the local Pantechnicon mirror was used as a contrastive
test corpus after `.sit` extraction and stack-only fingerprinting:

- Run directory: `import-runs/science-after-format-fixes`
- Archives processed: 26
- Extracted files classified as HyperCard stacks: 26
- Classified stacks imported successfully: 26
- Classified stack failures: 0
- Parser warnings/errors for understood stack structures: 0
- Extracted non-stack files skipped by fingerprinting: 135
- Archives with no classified `STAK` files: `dynRiskDemo68K.sit`,
  `dynRiskDemoPPC.sit`, and `edibleLandscaping.sit`

Contrastive failures that improved the parser:

- `fundamentalPhysicsDemo.sit` and `macCamWorkshop.sit` had `PAGE` blocks with
  no zero-card sentinel before the end of the 2048-byte page block. The local
  documentation says `LIST` records contain per-page entry counts, and using
  those counts removes the false "Premature end of 'PAGE'" warnings.
- `gsGraphinStuph.sit` contains two closely related stacks. Sanitizer runs showed
  both can trigger out-of-bounds WOBA mask row reads, but the color variant was
  the one that crashed under the normal build. The bitmap copy/fill paths now
  clip reads and writes to the allocated image/mask buffers.

The remaining 135 "ununderstood" Science entries are classified non-stack files
inside archives, not stack parser failures. Their Finder types include `TEXT`,
`PICT`, `APPL`, `Mcr$`, `Mcr0`, `MooV`, and icon/resource placeholder files.

## Reverse-Engineering Workflow

Use `--dumprawblocks` when a stack contains a skipped or partially understood
block:

```sh
stackimport --dumprawblocks /path/to/Stack
```

This writes raw `.data` files next to the generated JSON and media output. Compare
those dumps against the offset notes in this directory, then add a parser in
`CStackFile.cpp` and an entry in the coverage tables above.

## Notes By Topic

| Topic | Files |
| --- | --- |
| Stack block dispatch and stream layout | `Stack/ReadStackFile.txt`, `Stack/ReadBlock.txt` |
| Bettencourt 2011 guide summary | `DefinitiveGuide2011.md` |
| Backgrounds and cards | `Background/ReadBKGDBlock.txt`, `Card/ReadCARDBlock.txt` |
| Parts and part contents | `Part/ReadPartData.txt`, `Part/ReadPartContents.txt`, `Part/GetIcon.txt` |
| Font table | `FontTable/ReadFTBLBlock.txt` |
| Report templates | `ReportTemplate/ReadPRFTBlock.txt`, `ReportTemplate/ReadItemData.txt` |
| AddColor resources | `AddColor.md` |
| Resource fork coverage map | `ResourceForkCoverage.md` |
