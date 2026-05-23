# Bettencourt 2011 Format Notes

Source folded in from local file:
`~/Downloads/HC FILE FORMAT 2010.txt`

Title in source: "The Definitive Guide to the HyperCard Stack File Format for
HyperCard 2.0 to 2.4.1", by Rebecca Bettencourt, with thanks to Tyler Vano,
Michael Nichols, and Bill Atkinson. The local copy says it was updated
December 21, 2011.

This file summarizes the parts of that guide that fill gaps in the older local
xtalk notes. It is a derived index, not a full copy of the source text.

## Scope And Conventions

- Applies to stacks created by HyperCard 2.0 through 2.4.1.
- Older HyperCard 1.x stacks may differ.
- Integers and bit fields are big-endian.
- Strings use MacRoman.
- Middle-of-structure strings are null-terminated.
- End-of-structure strings may omit the null terminator.
- Variable structures may include a one-byte alignment pad to reach an even
  offset.

## Block Stream

Every stack data fork is a stream of blocks:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 4 | Block size, including this header |
| 4 | 4 | Four-character block type |
| 8 | 4 | Block ID |
| 12 | 4 | Filler, always zero |
| 16 | n | Type-specific data |

The current `CStackFile.cpp` reader stores bytes starting at offset 12 in
`blockData`, so the filler word appears at `blockData[0..3]` in parser code.

Block type list:

| Type | Meaning | Multiplicity |
| --- | --- | --- |
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

`STAK`, `MAST`, `LIST`, and `PAGE` blocks appear first in that order. `TAIL`
appears last. Other block order is not meaningful. Card order comes from `LIST`
and `PAGE`, not from the physical order of `CARD` blocks.

## MAST Block

`MAST` is the master reference object. It has ID `-1` and starts with four
unknown 32-bit fields after the common header. The rest is an array of 32-bit
address entries.

For each non-zero address entry:

| Bits | Meaning |
| --- | --- |
| high 24 bits | Block offset from start of file, divided by 32 |
| low 8 bits | Low byte of the target block ID |

The array does not include `STAK`, `MAST`, `FREE`, or `TAIL` blocks.

## LIST Block

`LIST` defines the segmented card index:

| Field | Meaning |
| --- | --- |
| page count | Number of `PAGE` blocks |
| page size | Usually 2048 bytes |
| page entry total | Total entries across all pages; should match card count |
| page entry size | Size of one card entry in a `PAGE` block |
| pages | Array of `PAGE` IDs and entry counts |

The page-entry count in each `LIST` entry is authoritative. Some real stacks do
not include a zero-card sentinel before the end of the fixed-size `PAGE` block;
parsers should read exactly the count supplied by `LIST` unless the `PAGE` block
ends earlier.

## PAGE Block

Each `PAGE` block belongs to the `LIST` block and contains `pageEntryCount`
entries, where the count comes from the corresponding `LIST` entry.

Each page entry contains:

| Field | Meaning |
| --- | --- |
| card ID | Target `CARD` block ID |
| extra bytes | `pageEntrySize - 4` bytes; first byte bit 4 is card marked state |

The remaining extra bytes are suspected search index data.

## BKGD And CARD Blocks

The guide provides the shared part and part-content structures used by both
backgrounds and cards. The current parser implements these in
`CStackFile::LoadLayerBlock`.

Important refinements from the guide:

- `BKGD` records next/previous background IDs.
- `CARD` records the containing `PAGE` ID and owning background ID.
- Part records split the type and flags into separate bytes.
- Part style uses only the low four bits.
- `partContent.partId` is positive for background parts and negative for card
  parts.
- Part content may either begin with a plain-text marker byte or with a styled
  text formatting-length field whose high bit is set.

## BMAP Block

`BMAP` contains bitmap bounds, mask bounds, image bounds, mask byte count, image
byte count, then WOBA-compressed mask and image bytes. The guide describes WOBA
instruction opcodes; the current implementation lives in `woba.cpp`.

Mask behavior:

- If mask data is present, use it.
- If mask data is missing but mask bounds are present, use the mask bounds.
- If neither is present, use the image as its own mask.

## FREE Block

`FREE` blocks have ID `0`, an always-zero filler, and a marker string identifying
free space. The parser skips them.

## STBL Block

The style table contains a style count, next style ID, then style records. Each
record includes a style ID, optional font ID, optional QuickDraw text style
bits, optional font size, and unknown/reserved fields.

The guide clarifies that `-1` means "no change" for font and size. Text style
uses QuickDraw bits: group, extend, condense, shadow, outline, underline,
italic, and bold.

## FTBL Block

The font table maps stack font IDs to font names:

| Field | Meaning |
| --- | --- |
| font count | Number of entries |
| font ID | Stack-local font ID |
| font name | MacRoman font name |
| align | Optional one-byte alignment |

## PRNT Block

`PRNT` is the print settings and report-template index block.

Documented fields:

| Offset | Field | Meaning |
| ---: | --- | --- |
| `0x0030` | page setup ID | ID of the `PRST` block |
| `0x0134` | template count | Number of report templates |
| after count | template entries | Fixed-size entries, 36 bytes each |

Each template entry contains:

| Field | Meaning |
| --- | --- |
| template ID | ID of a `PRFT` block |
| template name length | One-byte length |
| template name | MacRoman bytes |
| filler | Pad so the entry totals 36 bytes |

The rest of `PRNT` is still unknown in the guide.

## PRST Block

`PRST` is a HyperCard block header plus the classic QuickDraw Printing Manager
page setup record. Documented fields include printer driver version, resolution,
page and paper rectangles, first/last page, copy count, printing method, and
spool metadata.

Printing method values:

| Value | Meaning |
| ---: | --- |
| 0 | Draft |
| 1 | Deferred |

## PRFT Block

`PRFT` describes a report template.

Header fields include:

| Field | Meaning |
| --- | --- |
| units | `0` centimeters, `1` millimeters, `2` inches, `3` points/pixels |
| margins | Top, left, bottom, right |
| spacing | Height and width |
| cell size | Height and width |
| flags | Bit 8 left-to-right, bit 0 dynamic height |
| header | Pascal string with embedded control characters |

Header string control characters:

| Byte | Meaning |
| ---: | --- |
| `0x01` | Date |
| `0x02` | Time |
| `0x03` | Stack name |
| `0x04` | Page number |

At offset `0x0124`, the block contains a report item count followed by report
items.

Each report item includes:

| Field | Meaning |
| --- | --- |
| item size | Size including this item header |
| rectangle | Top, left, bottom, right |
| column count | Number of columns |
| flags | Change/invert/frame bits |
| text size / height | Text metrics |
| text style | QuickDraw text style bits |
| text align | `0` left, `1` center, `-1` right |
| contents | MacRoman string |
| text font | MacRoman font name |

Report item flag bits:

| Bit | Meaning |
| ---: | --- |
| 13 | change height |
| 12 | change style |
| 11 | change size |
| 10 | change font |
| 4 | invert |
| 3 | right frame |
| 2 | bottom frame |
| 1 | left frame |
| 0 | top frame |

## TAIL Block

`TAIL` has ID `-1`, an always-zero filler, then a length-prefixed tail string.
HyperCard 2.x uses a 15-byte tail string. The parser recognizes `TAIL` as the
end marker.

## HCcd And HCbg Resources

The guide also defines AddColor resources and explicitly includes both picture
resource and picture file entries. See `AddColor.md`.

## Remaining Unknowns

The guide still calls out these open questions:

- The rest of the `PRNT` block.
- The many fields still labeled unknown or "something".
