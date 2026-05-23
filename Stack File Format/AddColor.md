# AddColor Resources

Sources:

- <https://creysoft.com/xtalk/addcolor_format.htm>
- `~/Downloads/HC FILE FORMAT 2010.txt`, updated December 21, 2011

AddColor data is stored in resource-fork resources rather than stack-file
blocks. For each background or card, there may be a matching resource:

| Resource | Applies To | ID |
| --- | --- | --- |
| `HCbg` | Background | Same ID as the `BKGD` block |
| `HCcd` | Card | Same ID as the `CARD` block |

The resource contains AddColor object entries back-to-back until the end of the
resource. Values are big-endian.

## Rendering Model

AddColor is not native per-part color data. It draws color objects back-to-front
into a separate color overlay buffer: background layer first, then card layer.
That overlay is merged with the black-and-white HyperCard card contents using
QuickDraw's `or` pen mode, which effectively replaces white pixels with the
color overlay while preserving black pixels.

The AddColor XCMD can refresh the overlay on card changes, show visual effects,
and add, delete, hide, or show color elements independently of native HyperCard
parts.

## Entry Header

Each entry begins with an object type byte:

| Bits | Meaning |
| --- | --- |
| bit 7 | Hidden flag |
| bits 0-6 | Object type |

Known object types:

| Type | Object |
| ---: | --- |
| `0x01` | Button color |
| `0x02` | Field color |
| `0x03` | Rectangle |
| `0x04` | PICT resource |
| `0x05` | PICT file |

Recurring data types:

| Type | Meaning |
| --- | --- |
| Bevel | Highlight/shadow depth in pixels |
| Color | QuickDraw `RGBColor`, three unsigned 16-bit values |
| Rect | QuickDraw rectangle: top, left, bottom, right |

## Button Entry

| Size | Meaning |
| ---: | --- |
| 1 | Object type `0x01`, with high-bit hidden flag |
| 2 | Button ID |
| 2 | Bevel |
| 2 | Red value, 0...65535 |
| 2 | Green value, 0...65535 |
| 2 | Blue value, 0...65535 |

The rectangle is taken from the matching button on the card or background.

## Field Entry

| Size | Meaning |
| ---: | --- |
| 1 | Object type `0x02`, with high-bit hidden flag |
| 2 | Field ID |
| 2 | Bevel |
| 2 | Red value, 0...65535 |
| 2 | Green value, 0...65535 |
| 2 | Blue value, 0...65535 |

The rectangle is taken from the matching field on the card or background.

## Rectangle Entry

| Size | Meaning |
| ---: | --- |
| 1 | Object type `0x03`, with high-bit hidden flag |
| 2 | Top |
| 2 | Left |
| 2 | Bottom |
| 2 | Right |
| 2 | Bevel |
| 2 | Red value, 0...65535 |
| 2 | Green value, 0...65535 |
| 2 | Blue value, 0...65535 |

A rectangle is an independent colored rectangular surface.

## Picture Resource Entry

| Size | Meaning |
| ---: | --- |
| 1 | Object type `0x04`, with high-bit hidden flag |
| 2 | Top |
| 2 | Left |
| 2 | Bottom |
| 2 | Right |
| 1 | Transparent boolean |
| 1 | Resource name length |
| n | Resource name |

## Picture File Entry

| Size | Meaning |
| ---: | --- |
| 1 | Object type `0x05`, with high-bit hidden flag |
| 2 | Top |
| 2 | Left |
| 2 | Bottom |
| 2 | Right |
| 1 | Transparent boolean |
| 1 | File name length |
| n | File name |

Picture objects can be transparent, allowing lower color content to show through
white areas, or opaque relative to other AddColor objects.

## Implementation

`CStackFile::LoadLayerBlock` reads `HCbg`/`HCcd` resources after writing the
native card/background XML. It decodes object types `0x01`, `0x02`, `0x03`,
`0x04`, and `0x05`, and emits `<addcolorobject>` XML elements.
