meta:
  id: quicktime_mov
  application: QuickTime, MP4 ISO 14496-14 media
  file-extension: mov
  xref:
    mime: video/quicktime
    pronom: x-fmt/384
    wikidata: Q942350
  license: CC0-1.0
  endian: be
doc-ref: 'https://developer.apple.com/library/archive/documentation/QuickTime/QTFF/QTFFChap1/qtff1.html#//apple_ref/doc/uid/TP40000939-CH203-BBCGDDDF'
seq:
  - id: atoms
    type: atom_list
types:
  atom_list:
    seq:
      - id: items
        type: atom
        repeat: eos
  atom:
    seq:
      - id: len32
        type: u4
      - id: atom_type
        type: u4
      - id: len64
        type: u8
        if: len32 == 1
      - id: body
        size: len
    instances:
      len:
        value: 'len32 == 0 ? (_io.size - 8) : (len32 == 1 ? len64 - 16 : len32 - 8)'

# This excerpt records the upstream schema basis for the local wrapper. The
# wrapper also decodes common QuickTime sample table atoms (`mdhd`, `hdlr`,
# `stsd`, `stts`, `stsz`, `stco`, `co64`) used by Myst movie files.
