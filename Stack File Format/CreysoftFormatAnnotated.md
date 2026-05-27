# HyperCard File Format (Annotated)

## Provenance

- **Source material:** <https://creysoft.com/xtalk/hypercard_file_format2.htm>
- **Original author:** Mister Z. (Creysoft)
- **Retrieval date:** 2026-05-26
- **Conversion tool:** Manual HTML-to-Markdown conversion with structural preservation.
- **Local path:** `Stack File Format/CreysoftFormatAnnotated.md`
- **Relationship to source:** Derived and heavily annotated. The original text and
  structure are preserved as a base. All sections marked "There be Tygers" in the
  original have been filled in where the stackimport project has reverse-engineered
  the fields. Entirely new sections have been added for block types, resource types,
  and data structures not covered in the original.
- **Supporting evidence:** Corpus runs against the Pantechnicon stack collection;
  parser implementation in `CStackFile.cpp`, `woba.cpp`, `picture.cpp`; local format
  notes in `Stack File Format/*/*.txt`; Bettencourt 2011 guide summary in
  `DefinitiveGuide2011.md`.
- **Known omissions:** Some reserved/filler fields remain opaque even after
  annotation. Print settings (`PRNT`) has unknown internal fields beyond page setup
  ID and report-template index. AddColor unknown object types stop parsing for that
  resource. Code resources are preserved as raw data, not interpreted.

---

## Caveat Emptor!

Although originally intended by Bill Atkinson, the HyperCard file format has never
been officially published. The instructions in this file are simply what was deduced
by looking at various existing stacks and their differences.

**Warning:** The information in this document is not complete enough to allow the
creation of new HyperCard stacks, but maybe it can be helpful in reading existing
stacks and extracting precious data to keep it from being lost.

## Prerequisites

Being a file format from Classic MacOS (shipped 1987 through 2004), many of the
data types are from that era. All text is encoded in the MacRoman text encoding,
and many flags and data types are from the QuickDraw headers, or based on them. All
data is stored in Big-Endian format (like the Motorola 68000 used to do).

## The Block File Layout

A HyperCard stack is a stream of blocks, with a four-character type code and a 4-byte
signed ID number, terminated by a `TAIL` block. Each block has the following basic
layout:

| Offset | Size | Field | Meaning |
|--------|-----:|-------|---------|
| 0 | 4 | `blockSize` | Block size, including this header |
| 4 | 4 | `blockType` | Four-character block type code |
| 8 | 4 | `blockID` | Signed block ID number |
| 12 | 4 | `filler` | Always zero |

The `filler` word at offset 12 is always present but not documented in the original
Creysoft write-up. The current parser (`CStackFile.cpp`) reads bytes starting at
offset 12 into `blockData`, so the filler word appears at `blockData[0..3]` in parser
offsets.

All offsets in the block-specific sections below are **payload offsets**: they start
at byte 16 of the block in the file (i.e., after the 12-byte header plus the 4-byte
filler).

Block types:

| Type | Meaning | Multiplicity |
|------|---------|-------------|
| `STAK` | Stack header | Once |
| `MAST` | Master reference object | Once |
| `LIST` | Card index list | Once |
| `PAGE` | Card index page | Many |
| `BKGD` | Background | Many |
| `CARD` | Card | Many |
| `BMAP` | Card/background bitmap | Many |
| `FREE` | Free space removed by compacting | Many |
| `STBL` | Style table | Once |
| `FTBL` | Font table | Once |
| `PRNT` | Print settings and report-template index | Once |
| `PRST` | Page Setup settings | Once |
| `PRFT` | Report template | Many |
| `TAIL` | Tail object / end marker | Once |

`STAK`, `MAST`, `LIST`, and `PAGE` blocks appear first in that order. `TAIL` appears
last. Other block order is not meaningful. Card order comes from `LIST` and `PAGE`,
not from the physical order of `CARD` blocks.

---

## The Stack Block (`STAK`)

There is one block in the file representing the stack object. Its ID is always `-1`.

| Payload Offset | Size | Field | Meaning |
|----------------|-----:|-------|---------|
| 0x00 | 32 | -- | Unknown/reserved |
| 0x20 | 4 | `cardCount` | Number of cards in this stack |
| 0x24 | 4 | `firstCardID` | Block ID of one card in the stack |
| 0x28 | 4 | `listBlockID` | Block ID of the `LIST` block |
| 0x2C | 16 | -- | Unknown/reserved |
| 0x3C | 2 | `userLevel` | User Level setting (1...5) |
| 0x3E | 2 | -- | Unknown/reserved |
| 0x40 | 2 | `flags` | Stack protection flags (see below) |
| 0x42 | 22 | -- | Unknown/reserved |
| 0x54 | 4 | `createdByVersion` | `NumVersion` that created this stack |
| 0x58 | 4 | `lastCompactedVersion` | `NumVersion` that last compacted this stack |
| 0x5C | 4 | `lastEditedVersion` | `NumVersion` that last edited this stack |
| 0x60 | 4 | `firstEditedVersion` | `NumVersion` first edit |
| 0x64 | 320 | -- | Unknown/reserved |
| 0x1A4 | 4 | `fontTableBlockID` | Block ID of the `FTBL` block |
| 0x1A8 | 4 | `styleTableBlockID` | Block ID of the `STBL` block |
| 0x1AC | 2 | `cardHeight` | Card height in pixels (default: 342 if 0) |
| 0x1AE | 2 | `cardWidth` | Card width in pixels (default: 512 if 0) |
| 0x1B0 | 260 | -- | Unknown/reserved |
| 0x2B4 | 320 | `patterns[40]` | 40 patterns x 8 bytes each (8x8 1bpp bitmaps) |
| 0x3F4 | 512 | -- | Unknown/reserved |
| 0x5F4 | var | `stackScript` | Stack script as a null-terminated C string (MacRoman) |

The original document listed many of these fields as "There be Tygers." The
annotations above fill in the gaps using the stackimport parser and the Bettencourt
2011 guide.

### Stack Flags (offset 0x40)

| Bit | Flag | Meaning |
|----:|------|---------|
| 15 | `cantModify` | Stack cannot be modified |
| 14 | `cantDelete` | Stack cannot be deleted |
| 13 | `privateAccess` | Password-protected |
| 11 | `cantAbort` | Cannot abort scripts |
| 10 | `cantPeek` | Cannot peek at the stack |

The original document's bit numbering was: "Bit 10 is cantPeek, 11 is cantAbort, 13
is privateAccess, 14 is cantDelete, 15 is cantModify." This matches the parser
implementation.

### NumVersion Format (4 bytes)

Each version field is a BCD-encoded `NumVersion`:

| Byte | Meaning |
|-----:|---------|
| 0 | Major version (BCD) |
| 1 | Minor/revision (high nibble = minor, low nibble = sub-revision) |
| 2 | Stage (0x20 = dev, 0x40 = alpha, 0x60 = beta, 0x80 = release) |
| 3 | Prerelease revision number |

---

## The Master Reference Block (`MAST`)

> **New section.** Not in the original Creysoft document.

`MAST` is the master reference object. It has ID `-1` and maps block offsets and
low ID bytes for all blocks except `STAK`, `MAST`, `FREE`, and `TAIL`.

| Payload Offset | Size | Field | Meaning |
|----------------|-----:|-------|---------|
| 0x00 | 20 | -- | Header, unknown/reserved |
| 0x14 | 4*n | `entries[]` | Reference entries, 4 bytes each, to end of block |

### Reference Entry (4 bytes)

| Bits | Meaning |
|-----:|---------|
| 31-8 (high 24 bits) | Block offset from start of file, divided by 32 |
| 7-0 (low 8 bits) | Low byte of the target block ID |

A zero entry means unused/skip.

---

## The Style Table (`STBL`)

Styles for multi-styled text fields are stored as style formats in the style table
block of the stack, and only referenced throughout the stack.

| Payload Offset | Size | Field | Meaning |
|----------------|-----:|-------|---------|
| 0x00 | 4 | -- | Unknown/skip |
| 0x04 | 4 | `styleCount` | Number of styles |
| 0x08 | 2 | -- | Skipped |
| 0x0A | 2 | `nextStyleID` | Next available style ID |
| 0x0C | 2 | -- | Skipped |
| 0x0E | -- | `styles[]` | Start of style entries |

### Per Style Entry (24 bytes each)

| Entry Offset | Size | Field | Meaning |
|-------------|-----:|-------|---------|
| 0 | 2 | `styleID` | Style identifier |
| 2 | 8 | -- | Unknown/reserved |
| 10 | 2 | `fontID` | Font ID; -1 = inherit from field |
| 12 | 2 | `textStyleFlags` | QuickDraw text style bits (see below) |
| 14 | 2 | `fontSize` | Font size; -1 = inherit from field |
| 16 | 8 | -- | Unknown/reserved |

The original document listed the style entry as having 16 unknown bytes, a 2-byte
font ID, 2-byte style flags, 2-byte font size, and 2 unknown bytes. The annotations
above reflect the 24-byte structure verified against corpus stacks.

### Text Style Flags

| Bit | Flag |
|----:|------|
| 15 | `group` |
| 14 | `extend` |
| 13 | `condense` |
| 12 | `shadow` |
| 11 | `outline` |
| 10 | `underline` |
| 9 | `italic` |
| 8 | `bold` |
| 0 (all zero) | `plain` |

A font ID of -1 means "inherit from the containing field's styles." A font size of
-1 means "inherit." If both bytes of `textStyleFlags` are `0xFF`, the style is unset
and should be inherited.

---

## The Font Table (`FTBL`)

Since font IDs are not persistent across systems, HyperCard contains a table mapping
font names to IDs, so it can get away with storing font IDs in the stack file, but
still map the ID to the new one when it changes.

| Payload Offset | Size | Field | Meaning |
|----------------|-----:|-------|---------|
| 0x00 | 6 | -- | Unknown/skip |
| 0x06 | 2 | `numFonts` | Number of font entries |
| 0x08 | 4 | -- | Reserved |
| 0x0C | -- | `fonts[]` | Start of font entries |

### Per Font Entry (variable length)

| Entry Offset | Size | Field | Meaning |
|-------------|-----:|-------|---------|
| 0 | 2 | `fontID` | Font ID |
| 2 | var | `fontName` | Null-terminated MacRoman font name |
| after name | 0-1 | `align` | Optional alignment byte to reach even offset |

---

## The Page Table List (`LIST`)

Cards may be stored in an arbitrary order in a stack, so there is a list of pages
that, among other things, contains an ordered list of the card IDs in this stack. To
speed up insertions/deletions of cards in large stacks, this list has been segmented.
There is one `LIST` block listing all page tables, and then a bunch of page table
blocks listing the cards in order.

| Payload Offset | Size | Field | Meaning |
|----------------|-----:|-------|---------|
| 0x00 | 4 | `numPageTables` (fallback) | Used only if offset 0x04 is invalid |
| 0x04 | 4 | `numPageTables` | Primary page table count |
| 0x08 | 4 | -- | Unknown/skip |
| 0x0C | 2 | `cardBlockSize` (fallback) | Used only in compact layout |
| 0x0E | 2 | -- | Unknown/skip |
| 0x10 | 2 | `cardBlockSize` | Size of one card entry in `PAGE` blocks |
| 0x12 | 16 | -- | Unknown/reserved |
| 0x22 | -- | `pages[]` | Page table directory entries |

### Per Page Table Directory Entry (6 bytes consumed per iteration)

| Offset | Size | Field | Meaning |
|--------|-----:|-------|---------|
| 0 | 2 | -- | Unknown/skip |
| 2 | 4 | `pageTableID` | Block ID of the `PAGE` block |
| 6 | 2 | `pageEntryCount` | Number of card entries in this page |

The original document did not include `pageEntryCount`. This field is critical for
parsing: some real stacks do not include a zero-card sentinel before the end of a
fixed-size `PAGE` block. Parsers should read exactly the count supplied by `LIST`
unless the `PAGE` block ends earlier.

**Compact layout fallback:** If the value at offset 0x04 appears invalid (exceeds
the maximum possible page tables given the block size) but the value at offset 0x00
is valid, the code uses offset 0x00 for `numPageTables` and offset 0x0C for
`cardBlockSize`.

---

## The Page Table (`PAGE`)

The page table stores the order of the cards in the stack (because the actual card
data is kept in an arbitrary order in the file) and is segmented into several blocks.

| Payload Offset | Size | Field | Meaning |
|----------------|-----:|-------|---------|
| 0x00 | 12 | -- | Unknown/header |
| 0x0C | -- | `cardEntries[]` | Card entries, `cardBlockSize` bytes each |

### Per Card Entry (`cardBlockSize` bytes)

| Entry Offset | Size | Field | Meaning |
|-------------|-----:|-------|---------|
| 0 | 4 | `cardID` | Block ID of the `CARD` block; 0 = sentinel/end |
| 4 | 1 | `cardFlags` | Card flags; bit 4 = `marked` |
| 5..end | var | -- | Remaining bytes suspected to be search/index data |

The original document described the per-entry extra bytes as "There be Tygers." The
current understanding is that these are likely search index data. The `marked` flag
(bit 4 of the first extra byte) has been confirmed.

---

## Cards and Backgrounds (`CARD` and `BKGD`)

Both cards and backgrounds are stored in the same kind of block, parsed by a shared
layer parser. The layout differs slightly between `BKGD` and `CARD`.

### Layer Header

| Payload Offset | Size | Field | BKGD | CARD |
|----------------|-----:|-------|------|------|
| 0x00 | 4 | `filler1` | Unknown/filler | Same |
| 0x04 | 4 | `bitmapID` | ID of `BMAP` block for picture (0 = transparent) | Same |
| 0x08 | 2 | `flags` | Layer flags (see below) | Same |
| 0x0A | 14 | -- | Unknown/reserved | Same |
| 0x18 | 4 | `owner` | **Next background ID** (BKGD linked list) | **Background ID** that owns this card |
| 0x1C | -- | -- | Start of numParts for BKGD | numParts at offset 0x1C for CARD? |

The correct offsets differ between BKGD and CARD:

| Field | BKGD Offset | CARD Offset |
|-------|-------------|-------------|
| `numParts` | 0x1A (26) | 0x1E (30) |
| `numContents` | 0x22 (34) | 0x26 (38) |
| Parts start at | 0x26 (38) | 0x2A (42) |

The original document listed many of these fields as "There be Tygers." The
background and card offsets are now confirmed against corpus stacks.

### Layer Flags (offset 0x08)

| Bit | Flag | Meaning |
|----:|------|---------|
| 14 | `cantDelete` | Layer cannot be deleted |
| 13 | `showPict` | Show card picture (inverted: 0 = hidden) |
| 11 | `dontSearch` | Exclude from search |

Note: `showPict` is inverted: the flag is set when the picture is *hidden*.

### Part Record Structure

Each part record is variable-length. The first 2 bytes give the total record length.

| Offset | Size | Field | Meaning |
|--------|-----:|-------|---------|
| 0 | 2 | `partLength` | Total bytes in this part record (minimum 30) |
| 2 | 2 | `partID` | Part identifier |
| 4 | 2 | `flagsAndType` | High byte = part type, low byte = flags |
| 6 | 2 | `rect.top` | Top coordinate |
| 8 | 2 | `rect.left` | Left coordinate |
| 10 | 2 | `rect.bottom` | Bottom coordinate |
| 12 | 2 | `rect.right` | Right coordinate |
| 14 | 2 | `moreFlags` | Low nibble = style, high byte = secondary flags |
| 16 | 2 | `titleWidth` / `lastSelectedLine` | Buttons: title width; Fields: last selected line |
| 18 | 2 | `iconID` / `selectedLine` | Buttons: icon ID; Fields: (first) selected line |
| 20 | 2 | `textAlign` | 0 = left/default, 1 = center, -1 = right, -2 = force left |
| 22 | 2 | `textFontID` | Font table reference |
| 24 | 2 | `textSize` | Font size |
| 26 | 2 | `textStyleFlags` | Text style bits (see STBL style flags) |
| 28 | 2 | `textHeight` | Line height (fields only) |
| 30 | var | `name` | Null-terminated MacRoman name |
| after name + 1 | var | `script` | Null-terminated MacRoman HyperTalk script |
| end | 0-1 | `align` | Alignment byte if total is odd |

The original document listed `flagsAndType` as separate bytes (type byte + flags
byte). In practice, they are stored as a single big-endian 16-bit word where the
high byte is the part type and the low byte is the flags.

### Part Type and Flags (`flagsAndType`, offset 4)

**Part type** (high byte of `flagsAndType`):

| Value | Type |
|------:|------|
| 1 | Button |
| 2 | Field |

**Part flags** (low byte of `flagsAndType`):

| Bit | Button | Field |
|----:|--------|-------|
| 7 | -- | `visible` (inverted) / `hidden` |
| 5 | -- | `dontWrap` |
| 4 | -- | `dontSearch` |
| 3 | -- | `sharedText` |
| 2 | -- | `fixedLineHeight` (inverted) |
| 1 | -- | `autoTab` |
| 0 | `enabled` (inverted) | `lockText` / `disabled` |

Note: the original document listed bit 7 as "hidden" and bit 0 as
"disabled/lockText." The parser implementation treats these as inverted (visible
and enabled when the bit is clear).

### Part Style and Secondary Flags (`moreFlags`, offset 14)

**Style** (low nibble, `moreFlags & 0x0F`):

| Value | Button Style | Field Style |
|------:|-------------|-------------|
| 0 | transparent | transparent |
| 1 | opaque | opaque |
| 2 | rectangle | rectangle |
| 3 | roundrect | -- |
| 4 | shadow | shadow |
| 5 | checkbox | -- |
| 6 | radio button | -- |
| 7 | -- | scrolling |
| 8 | standard | -- |
| 9 | default | -- |
| 10 | oval | -- |
| 11 | popup | -- |

**Secondary flags** (high byte, `moreFlags >> 8`):

| Bit | Button | Field |
|----:|--------|-------|
| 7 | `showName` | `autoSelect` |
| 6 | `highlight` | `showLines` |
| 5 | `autoHighlight` | `wideMargins` |
| 4 | `sharedHighlight` (inverted) | `multipleLines` |
| 3-0 | `family` number | Reserved |

The original document noted: "bit 15 is showName/autoSelect, bit 14 is
highlight/showLines, bit 13 is wideMargins/autoHighlight, bit 12 is
sharedHighlight/multipleLines, bits 11 through 8 is the button family number." This
is consistent with the parser but uses bit-15 numbering for what the parser calls
bit 7 of the high byte (same physical bit).

### Part Content Records

After all part records, content records follow.

| Offset | Size | Field | Meaning |
|--------|-----:|-------|---------|
| 0 | 2 | `partID` | Positive = background part; negative = card part (negate for actual ID) |
| 2 | 2 | `partLength` | Length of data after this 4-byte prefix |
| 4 | 2 | `stylesLength` | If > 32767: style data present, actual length = `stylesLength - 32768` |
| 6 | S-2 | `styleRuns[]` | Style run data (see below) |
| 4+S | var | `text` | Text data, null-terminated MacRoman |

Total content record = `4 + partLength`, even-aligned.

### Per Style Run (4 bytes each)

| Offset | Size | Field | Meaning |
|--------|-----:|-------|---------|
| 0 | 2 | `startOffset` | Character offset into text |
| 2 | 2 | `styleID` | Index into `STBL` style table |

When `stylesLength <= 32767`, the content is mono-styled (no style runs). When
`stylesLength > 32767`, there are `(stylesLength - 32768 - 2) / 4` style run entries
followed by the text.

The original document described styled text as having style data "prepended" when the
high bit of `stylesLength` is set. The parser confirms this: the style run array sits
between the `stylesLength` field and the text, so it is "prepended" relative to the
text.

### Layer Name and Script (after all contents)

After all content records:

| Field | Meaning |
|-------|---------|
| `layerName` | Null-terminated MacRoman name |
| `layerScript` | Null-terminated MacRoman HyperTalk script |

### OSA Script Data (optional, after layer script)

| Offset | Size | Field | Meaning |
|--------|-----:|-------|---------|
| 0 | 2 | `osaOffset` | Offset from end of this field to OSA script |
| 2 | 2 | `osaLength` | Length of OSA script |
| 4 | var | `header` | Remainder of header |
| after header | var | `osaScript` | OSA script |

The `scriptType` field (at a fixed offset in the layer header, before the parts
start) indicates the scripting language: 0 = HyperTalk, `'WOSA'` = compiled OSA
language (e.g., AppleScript).

---

## Bitmap Blocks (`BMAP`)

> **New section.** Not in the original Creysoft document.

`BMAP` blocks contain WOBA-compressed 1-bit bitmap data used by cards and
backgrounds. They are referenced by the `bitmapID` field in `CARD` and `BKGD`
blocks.

### BMAP Header (payload offsets)

| Payload Offset | Size | Field | Meaning |
|----------------|-----:|-------|---------|
| 0x00 | 8 | -- | Unknown/header |
| 0x08 | 4 | -- | Unknown |
| 0x0C | 2 | `totalRect.top` | Total bounds rectangle |
| 0x0E | 2 | `totalRect.left` | |
| 0x10 | 2 | `totalRect.bottom` | |
| 0x12 | 2 | `totalRect.right` | |
| 0x14 | 2 | `maskBoundRect.top` | Mask bounds rectangle |
| 0x16 | 2 | `maskBoundRect.left` | |
| 0x18 | 2 | `maskBoundRect.bottom` | |
| 0x1A | 2 | `maskBoundRect.right` | |
| 0x1C | 2 | `pictureBoundRect.top` | Picture bounds rectangle |
| 0x1E | 2 | `pictureBoundRect.left` | |
| 0x20 | 2 | `pictureBoundRect.bottom` | |
| 0x22 | 2 | `pictureBoundRect.right` | |
| 0x24 | 8 | -- | Unknown |
| 0x2C | 4 | `maskDataLength` | Mask data byte count (0 = no mask) |
| 0x30 | 4 | `pictureDataLength` | Picture data byte count (0 = no picture) |
| 0x34 | var | `data` | Mask data (if `maskDataLength` > 0), then picture data |

### Mask Behavior

- If mask data is present (`maskDataLength > 0`), use it.
- If mask data is absent but mask bounds are present, use the mask bounds.
- If neither is present, use the picture as its own mask.

### WOBA Opcodes

The bitmap data is compressed using the WOBA (Word-Oriented Bitmap Algorithm)
encoding. Row references can refer to rows outside the allocated image on some
stacks; the parser clips reads and writes defensively.

| Opcode Range | Name | Description |
|-------------|------|-------------|
| 0x00-0x7F | zeros+data | Low nibble = zero bytes, high nibble = data bytes following |
| 0x80 | uncompressed | Raw row data follows |
| 0x81 | white row | Fill row with 0x00 |
| 0x82 | black row | Fill row with 0xFF |
| 0x83 | pattern | 1-byte operand = fill byte |
| 0x84 | last pattern | Reuse previous pattern |
| 0x85 | previous row | Copy row y-1 |
| 0x86 | two rows back | Copy row y-2 |
| 0x87 | three rows back | Copy row y-3 |
| 0x88 | delta 16,0 | dx=16, dy=0 |
| 0x89 | delta 0,0 | dx=0, dy=0 |
| 0x8A | delta 0,1 | dx=0, dy=1 |
| 0x8B | delta 0,2 | dx=0, dy=2 |
| 0x8C | delta 1,0 | dx=1, dy=0 |
| 0x8D | delta 1,1 | dx=1, dy=1 |
| 0x8E | delta 2,2 | dx=2, dy=2 |
| 0x8F | delta 8,0 | dx=8, dy=0 |
| 0xA0-0xBF | repeat | Low 5 bits + 1 = repeat count for next opcode |
| 0xC0-0xDF | bulk data | Low 5 bits * 8 = data bytes following |
| 0xE0-0xFF | bulk zeros | Low 5 bits * 16 = zero bytes |

---

## Free Blocks (`FREE`)

> **New section.** Not in the original Creysoft document.

`FREE` blocks have ID `0`, an always-zero filler, and represent reusable space in
the block stream. They are removed by compacting. The parser skips them.

---

## Tail Block (`TAIL`)

> **Expanded from the original document.**

`TAIL` has ID `-1`, an always-zero filler, then a length-prefixed tail string.
HyperCard 2.x uses a 15-byte tail string. The parser recognizes `TAIL` as the end
marker and stops reading the block stream.

---

## Print Settings Block (`PRNT`)

> **New section.** Not in the original Creysoft document.

`PRNT` stores print settings and report-template index references.

| Payload Offset | Size | Field | Meaning |
|----------------|-----:|-------|---------|
| 0x00..0x23 | 36 | -- | Unknown print settings |
| 0x24 | 2 | `pageSetupID` | Block ID of the `PRST` block |
| 0x26..0x127 | -- | -- | Unknown |
| 0x128 | 2 | `reportTemplateCount` | Number of report template references |
| 0x12A | 36*n | `templates[]` | Report template references, 36 bytes each |

### Per Report Template Reference (36 bytes)

| Entry Offset | Size | Field | Meaning |
|-------------|-----:|-------|---------|
| 0 | 4 | `templateID` | Block ID of a `PRFT` block |
| 4 | 1 | `nameLen` | Pascal string length (max 31) |
| 5 | nameLen | `name` | MacRoman template name |
| after name | var | -- | Filler to reach 36 bytes total |

Most of the `PRNT` block remains unknown beyond `pageSetupID` and the report template
index.

---

## Page Setup Block (`PRST`)

> **New section.** Not in the original Creysoft document.

`PRST` is a HyperCard block header plus the classic QuickDraw Printing Manager page
setup record.

| Payload Offset | Size | Field | Meaning |
|----------------|-----:|-------|---------|
| 0x00 | 4 | -- | Skipped |
| 0x04 | 2 | `printerDriverVersion` | |
| 0x06 | 2 | `iDev` | |
| 0x08 | 2 | `vertResol` | Vertical resolution |
| 0x0A | 2 | `horizResol` | Horizontal resolution |
| 0x0C | 2 | `pageRect.top` | |
| 0x0E | 2 | `pageRect.left` | |
| 0x10 | 2 | `pageRect.bottom` | |
| 0x12 | 2 | `pageRect.right` | |
| 0x14 | 2 | `paperRect.top` | |
| 0x16 | 2 | `paperRect.left` | |
| 0x18 | 2 | `paperRect.bottom` | |
| 0x1A | 2 | `paperRect.right` | |
| 0x1C | 2 | `printerDeviceNumber` | |
| 0x1E | 2 | `pageV` | |
| 0x20 | 2 | `pageH` | |
| 0x22 | 1 | `port` | |
| 0x23 | 1 | `feedType` | |
| 0x24 | 2 | `iDev2` | |
| 0x26 | 2 | `vertResol2` | |
| 0x28 | 2 | `horizResol2` | |
| 0x2A | 2 | `pageRect2.top` | |
| 0x2C | 2 | `pageRect2.left` | |
| 0x2E | 2 | `pageRect2.bottom` | |
| 0x30 | 2 | `pageRect2.right` | |
| 0x32 | 16 | -- | Reserved |
| 0x42 | 2 | `firstPage` | |
| 0x44 | 2 | `lastPage` | |
| 0x46 | 2 | `numCopies` | |
| 0x48 | 1 | `printingMethod` | 0 = draft, 1 = deferred |
| 0x49 | 1 | -- | Reserved |

---

## Report Template Block (`PRFT`)

> **New section.** Not in the original Creysoft document.

`PRFT` describes a report template used for printing.

### Header Fields

| Payload Offset | Size | Field | Meaning |
|----------------|-----:|-------|---------|
| 0x04 | 1 | `units` | 0 = centimeters, 1 = millimeters, 2 = inches, 3 = points |
| 0x06 | 2 | `margins.top` | |
| 0x08 | 2 | `margins.left` | |
| 0x0A | 2 | `margins.bottom` | |
| 0x0C | 2 | `margins.right` | |
| 0x0E | 2 | `spacing.height` | |
| 0x10 | 2 | `spacing.width` | |
| 0x12 | 2 | `cellSize.height` | |
| 0x14 | 2 | `cellSize.width` | |
| 0x16 | 2 | `flags` | Bit 8 = leftToRight, bit 0 = dynamicHeight |
| 0x18 | 1 | `headerLen` | Pascal string length |
| 0x19 | headerLen | `header` | Header text with embedded control characters |

### Header String Control Characters

| Byte | Meaning |
|-----:|---------|
| 0x01 | Date |
| 0x02 | Time |
| 0x03 | Stack name |
| 0x04 | Page number |

### Report Items

| Payload Offset | Size | Field | Meaning |
|----------------|-----:|-------|---------|
| 0x118 | 2 | `itemCount` | Number of report items |
| 0x11A | var | `items[]` | Report items, variable size |

### Per Report Item (variable length)

| Entry Offset | Size | Field | Meaning |
|-------------|-----:|-------|---------|
| 0 | 2 | `itemSize` | Total entry size including this header |
| 2 | 2 | -- | Padding/unknown |
| 4 | 2 | `rect.left` | |
| 6 | 2 | `rect.top` | |
| 8 | 2 | `rect.right` | |
| 10 | 2 | `rect.bottom` | |
| 12 | 2 | `columns` | Number of columns |
| 14 | 2 | `flags` | See below |
| 16 | 2 | `textSize` | |
| 18 | 1 | `textStyle` | QuickDraw text style bits |
| 20 | 2 | `textAlign` | 0 = left, 1 = center, -1 = right |
| 22 | var | `contents` | Null-terminated MacRoman string |
| after contents | var | `textFont` | Null-terminated MacRoman font name |

### Report Item Flags (offset 14)

| Bit | Flag | Meaning |
|----:|------|---------|
| 13 | `changeHeight` | |
| 12 | `changeStyle` | |
| 11 | `changeSize` | |
| 10 | `changeFont` | |
| 4 | `invert` | |
| 3 | `frame.right` | |
| 2 | `frame.bottom` | |
| 1 | `frame.left` | |
| 0 | `frame.top` | |

---

## Resource Fork Data

> **New section.** Not in the original Creysoft document.

Classic Mac stack files store additional data in the resource fork, separate from the
block stream. The following resource types are relevant:

### Standard Resources

| Type | Meaning | Status |
|------|---------|--------|
| `ICON` | 32x32 1-bit icon (128 bytes) | Converted to PBM/PNG |
| `ICN#` | 32x32 icon with mask | Converted to PBM/PNG |
| `PICT` | QuickDraw picture | Exported with synthetic 512-byte header; PNG via adapter |
| `CURS` | 16x16 cursor with mask and hotspot | Converted to PBM/PNG |
| `PAT ` | Single 8x8 1-bit pattern | Converted to PNG |
| `PAT#` | Pattern list | Converted to PNG per pattern |
| `SICN` | 16x16 small icon list | Converted to PNG |
| `snd ` | Sound resource | Exported raw and converted to WAV |

### AddColor Resources (`HCbg` and `HCcd`)

AddColor data is stored in resource-fork resources for backgrounds and cards:

| Resource | Applies To | ID |
|----------|-----------|-----|
| `HCbg` | Background | Same ID as the `BKGD` block |
| `HCcd` | Card | Same ID as the `CARD` block |

#### AddColor Rendering Model

AddColor is not native per-part color data. It draws color objects back-to-front into
a separate color overlay buffer: background layer first, then card layer. That overlay
is merged with the black-and-white HyperCard card contents using QuickDraw's `or` pen
mode, which effectively replaces white pixels with the color overlay while preserving
black pixels.

#### AddColor Entry Header

Each entry begins with an object type byte:

| Bits | Meaning |
|-----:|---------|
| bit 7 | Hidden flag |
| bits 0-6 | Object type |

Known object types:

| Type | Object |
|-----:|--------|
| 0x01 | Button color |
| 0x02 | Field color |
| 0x03 | Rectangle |
| 0x04 | PICT resource |
| 0x05 | PICT file |

#### Button Color Entry (0x01)

| Size | Field | Meaning |
|-----:|-------|---------|
| 1 | `type` | Object type 0x01, high bit = hidden |
| 2 | `buttonID` | Button ID |
| 2 | `bevel` | Highlight/shadow depth in pixels |
| 2 | `red` | Red value, 0...65535 |
| 2 | `green` | Green value, 0...65535 |
| 2 | `blue` | Blue value, 0...65535 |

Rectangle is taken from the matching button on the card or background.

#### Field Color Entry (0x02)

| Size | Field | Meaning |
|-----:|-------|---------|
| 1 | `type` | Object type 0x02, high bit = hidden |
| 2 | `fieldID` | Field ID |
| 2 | `bevel` | Highlight/shadow depth in pixels |
| 2 | `red` | Red value, 0...65535 |
| 2 | `green` | Green value, 0...65535 |
| 2 | `blue` | Blue value, 0...65535 |

Rectangle is taken from the matching field on the card or background.

#### Rectangle Entry (0x03)

| Size | Field | Meaning |
|-----:|-------|---------|
| 1 | `type` | Object type 0x03, high bit = hidden |
| 2 | `top` | |
| 2 | `left` | |
| 2 | `bottom` | |
| 2 | `right` | |
| 2 | `bevel` | Highlight/shadow depth in pixels |
| 2 | `red` | Red value, 0...65535 |
| 2 | `green` | Green value, 0...65535 |
| 2 | `blue` | Blue value, 0...65535 |

#### PICT Resource Entry (0x04)

| Size | Field | Meaning |
|-----:|-------|---------|
| 1 | `type` | Object type 0x04, high bit = hidden |
| 2 | `top` | |
| 2 | `left` | |
| 2 | `bottom` | |
| 2 | `right` | |
| 1 | `transparent` | Boolean |
| 1 | `nameLen` | Resource name length |
| n | `name` | Resource name |

#### PICT File Entry (0x05)

| Size | Field | Meaning |
|-----:|-------|---------|
| 1 | `type` | Object type 0x05, high bit = hidden |
| 2 | `top` | |
| 2 | `left` | |
| 2 | `bottom` | |
| 2 | `right` | |
| 1 | `transparent` | Boolean |
| 1 | `nameLen` | File name length |
| n | `name` | File name |

Unknown object types (beyond 0x05) abort AddColor parsing for that resource.

### Code Resources

| Type | Meaning | Platform | Status |
|------|---------|----------|--------|
| `XCMD` | External command | 68K | Raw export + disassembly |
| `XFCN` | External function | 68K | Raw export + disassembly |
| `xcmd` | External command | PowerPC | Raw export + disassembly |
| `xfcn` | External function | PowerPC | Raw export + disassembly |

Code resources are preserved as raw data with JSON metadata. They are not decoded or
executed. 68K disassembly is emitted as `resource-disassembly/*.s` inside the output
package.

---

## Processing Order

> **New section.** Not in the original Creysoft document.

The parser processes blocks in this order:

1. Stream all blocks from file into an internal map.
2. Parse `STAK` (extracts cross-block IDs, patterns, card dimensions, versions, script).
3. Load resource fork.
4. Parse `FTBL` (font table).
5. Parse `STBL` (style table, depends on FTBL).
6. Iterate blocks in order:
   - `BMAP`: decode WOBA or write raw.
   - `BKGD`: parse layer (must come before CARDs for owner lookups).
   - `MAST`: parse master references.
   - `PRNT`: parse print settings.
   - `PRST`: parse page setup.
   - `PRFT`: parse report templates.
7. Parse `LIST` -> `PAGE` -> `CARD` chain (CARD parsing needs backgrounds loaded).
8. Write output indexes.

---

## Remaining Unknowns

Fields still labeled "unknown" or "reserved" in this document:

- Most of the `PRNT` block beyond `pageSetupID` and the report-template index.
- Many fields in the `STAK` block between the version numbers and the font/style
  table IDs.
- The extra per-card bytes in `PAGE` entries beyond the card ID and marked flag.
- Some reserved/filler fields in `BKGD` and `CARD` layer headers.
- AddColor object types beyond 0x05.
- Internal code resource structure beyond raw export and 68K disassembly.

---

*Original document written up by Mister Z. Annotations and new sections from the
stackimport project, based on corpus-validated parser implementation and the
Bettencourt 2011 guide.*
