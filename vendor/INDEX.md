# Vendored Dependency Index

This file is the source-of-truth index for third-party code checked into
`vendor/`. Keep it current whenever a dependency is added, removed, upgraded, or
locally patched.

## Summary

| Path | Dependency | Role | License | Local Integration | Local Modifications |
| --- | --- | --- | --- | --- | --- |
| `rsrcd/` | rsrcd | Header-only C++ resource fork parser derived from rsrcdump | MIT, see `rsrcd/LICENSE` | `vendor_rsrcd` interface target; linked into `stackimport_static` | No upstream source changes. Added `rsrcd/STACKIMPORT_VENDOR.md` and CMake integration in `vendor/CMakeLists.txt`. |
| `quill/` | Quill 11.1.0 | Header-only C++ logging backend used by stackimport diagnostics | MIT, see `quill/LICENSE` | `vendor_quill` interface target; linked into `stackimport_static`; stackimport's logging facade uses Quill console sinks | No upstream source changes. Added `quill/STACKIMPORT_VENDOR.md` and CMake wrapper in `vendor/CMakeLists.txt`. |
| `rang/` | rang 3.2 | Header-only terminal color support for log/dump formatting | Unlicense, see `rang/LICENSE` | `vendor_rang` interface target; linked into `stackimport_static` for terminal color helpers | No upstream source changes. Added `rang/STACKIMPORT_VENDOR.md` and CMake wrapper in `vendor/CMakeLists.txt`. |
| `rapidjson/` | RapidJSON 1.1.0 | Header-only JSON DOM/writer used by `stackimport_static` | MIT, see `rapidjson/license.txt` | `vendor_rapidjson` interface target; linked into `stackimport_static` | No upstream source changes. Added `rapidjson/STACKIMPORT_VENDOR.md` and CMake wrapper in `vendor/CMakeLists.txt`. |
| `resource_dasm/` | `resource_dasm` / `libresource_file` snapshot | Classic Mac resource fork and resource conversion reference implementation | MIT, see `resource_dasm/LICENSE` | Built by `vendor_resource_dasm` external project; included in `stackimport-vendor-static` | Local CMake patch makes warnings-as-errors configurable, disables C++ extensions, removes hardcoded `-O2`, declares `DISABLE_SDL`, links audio tools through `resource_file`, and preserves the static `resource_file` library build. Local integration forwards C++23/C17 and strict flags, including `-Wpedantic`/`-Wformat=2` with a Clang zero-length-array compatibility suppression. |
| `phosg/` | `phosg` snapshot | Required support library for `resource_dasm` | MIT, see `phosg/src/LICENSE` | Built by `vendor_phosg` external project before `vendor_resource_dasm` | Local CMake patch disables C++ extensions and makes warnings-as-errors configurable so the wrapper controls the same standard and warning policy as the rest of the build. Local integration forwards C++23/C17 and strict flags, including `-Wpedantic`/`-Wformat=2`. |
| `deark/` | Deark 1.7.2 | Broad legacy decoder/reference for containers and image formats | MIT-style Deark license, see `deark/COPYING`; bundled foreign-code notices under `deark/foreign/` | Built by `vendor_deark` custom target; archives are included in `stackimport-vendor-static` | Local source patch adds one explicit allocation-count cast in `src/deark-modules.c`. Local integration passes C17 and a strict warning-as-error-compatible Make flag set, with compatibility suppressions for known legacy conversion/string/undef/shadow patterns. |
| `stb/` | `stb_image_write.h` | Header-only PNG/image writer candidate | MIT or public domain, license text embedded at end of `stb/stb_image_write.h` | `vendor_stb_image_write` interface target | No upstream source changes intended. CMake wrapper only. |
| `dr_wav/` | `dr_wav.h` | Header-only WAV reader/writer candidate | Public domain or MIT-0, license text embedded at end of `dr_wav/dr_wav.h` | `vendor_dr_wav` interface target | No upstream source changes intended. CMake wrapper only. |
| `snd2wav/` | `snd2wav` snapshot at `d6900ad35ba4da1fb488fe006f64a7977d98b32c` | Legacy Mac `snd ` to WAV conversion helper currently used by sound conversion paths | No explicit license file found in snapshot; source copyright headers state all rights reserved | `vendor_snd2wav` static target; linked into `stackimport_static` | Converted from a git submodule to checked-in vendored source. Added CMake wrapper in `vendor/CMakeLists.txt` and local provenance note in `snd2wav/STACKIMPORT_VENDOR.md`. Builds with required C++23, extensions off, and the shared vendor warning baseline plus targeted legacy-noise suppressions. |
| `ppcd/` | PPCD snapshot at `58f7e6df284fa4cd5a9a734b7a44d1851dcdaf16` | PowerPC instruction disassembler candidate for XCMD/XFCN reverse engineering | CC0-1.0, see `ppcd/LICENSE` | `vendor_ppcd` static target built with required C++23 and extensions off | Added `ppcd/STACKIMPORT_VENDOR.md` and CMake wrapper in `vendor/CMakeLists.txt`. Upstream `CommonDefs.h` is stored as `Commondefs.h` to match the include spelling on case-sensitive filesystems. Local source patch: one `char *` string-literal pointer changed to `const char *` in `ppcd.cpp`. Builds with the shared vendor warning baseline plus targeted legacy-noise suppressions. |

## Build Targets

The main vendor entry points are defined in `vendor/CMakeLists.txt`:

| Target | Purpose |
| --- | --- |
| `vendor-header-libraries` | Makes header-only interface targets available. |
| `vendor_rsrcd` | Exposes the vendored rsrcd resource fork parser header. |
| `vendor_quill` | Exposes the vendored Quill logging headers with stackimport's no-exceptions configuration. |
| `vendor_rang` | Exposes the vendored rang terminal color header. |
| `vendor_ppcd` | Builds the vendored PPCD PowerPC disassembler as a static library. |
| `vendor_snd2wav` | Builds the vendored local `snd2wav` helper as a static library. |
| `vendor-tools` | Builds header-only targets plus Deark, phosg, and resource_dasm. |
| `vendor-static-artifacts` | Builds static artifacts used by `stackimport-vendor-static`. |
| `stackimport-vendor-static` | Top-level target that combines `libstackimport.a` with vendored static archives. |

The wrapper sets C17 and C++23 where supported, disables compiler extensions for
CMake-based dependencies, and forwards configured static-analysis tools to those
CMake builds.

## Maintenance Rules

- Preserve upstream license files in place.
- Record every local source patch in the `Local Modifications` column above.
- Prefer CMake wrapper changes over editing vendored source.
- If a dependency is upgraded, update the dependency/version text, license path,
  and modification notes in this file in the same change.
- If a dependency has foreign bundled code, keep its notices near that code and
  summarize them here.
