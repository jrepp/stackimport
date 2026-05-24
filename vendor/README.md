# Vendored Conversion Tools

This directory contains permissively licensed source snapshots used for local
conversion and reverse-engineering work. They are intentionally isolated from
the main `stackimport` target; build them with the CMake `vendor-tools` target
when needed.

See `INDEX.md` for the maintained dependency index, including license paths,
local integration targets, and modification status.

## Contents

| Path | Project | Purpose | License |
| --- | --- | --- | --- |
| `quill/` | Quill 11.1.0 | Header-only C++ logging library used by stackimport diagnostics | MIT, see `quill/LICENSE` |
| `rang/` | rang 3.2 | Header-only terminal color support for log and dump formatting | Unlicense, see `rang/LICENSE` |
| `resource_dasm/` | `resource_dasm` / `libresource_file` | Classic Mac resource fork parsing, PICT/image/audio/code resource conversion and disassembly helpers | MIT, see `resource_dasm/LICENSE` |
| `phosg/` | `phosg` | Required dependency for `resource_dasm` | MIT, see `phosg/src/LICENSE` |
| `deark/` | Deark 1.7.2 | Broad legacy file/container decoder, including MacBinary, resource forks, BinHex, StuffIt, and PICT coverage | MIT-style, see `deark/COPYING`; bundled third-party notices live in `deark/foreign/` |
| `stb/` | `stb_image_write.h` | Single-header PNG writer candidate for direct image conversion | MIT or public domain, see license text at the end of `stb/stb_image_write.h` |
| `dr_wav/` | `dr_wav.h` | Single-header WAV writer/reader candidate for audio conversion | Public domain or MIT-0, see license text at the end of `dr_wav/dr_wav.h` |
| `ppcd/` | PPCD | Lightweight PowerPC instruction disassembler candidate for preserved PPC code resources | CC0-1.0, see `ppcd/LICENSE` |

## CMake Targets

The top-level project exposes:

```sh
cmake --build build --target vendor-tools --parallel
```

That target builds:

- `vendor_deark`
- `vendor_phosg`
- `vendor_resource_dasm`
- `vendor_ppcd`
- header-only interface targets for `stb_image_write` and `dr_wav`

The vendored tool path is optional and separate from the default `stackimport`
library/executable build.

The wrapper builds CMake-based vendored projects with C++23, C17, required
standards, and disabled compiler extensions. Deark is Make-based, so the wrapper
passes C17 and stricter warning flags through `CFLAGS`.

Static-analysis options from the top-level CMake build are forwarded to
CMake-based vendored projects:

```sh
cmake -S . -B build-analysis \
  -DSTACKIMPORT_ENABLE_CLANG_TIDY=ON \
  -DSTACKIMPORT_ENABLE_CLANG_STATIC_ANALYZER=ON \
  -DSTACKIMPORT_ENABLE_CPPCHECK=ON
cmake --build build-analysis --target vendor_resource_dasm --parallel
```

Deark does not consume CMake analyzer properties because it is Make-based; use
its generated objects and `compile_commands.json`-style tooling separately if it
needs deeper analysis.

## Importer Integration

The bulk importer catalogs external binary data even before these converters are
called. See these run outputs:

- `external-binary-files.tsv`
- `external-binary-usage.tsv`
- `embedded-files.tsv`
- `embedded-file-usage.tsv`
- `binary-chunks.tsv`

These files make it possible to decide which vendored converter should be used
for each external artifact or embedded output.
