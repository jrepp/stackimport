# rang vendoring notes

Source: https://github.com/agauniyal/rang

Version: v3.2 release archive

License: Unlicense. See `LICENSE`.

Scope: this repo vendors the upstream source archive intact. The active
integration only needs `include/rang.hpp`, but the upstream CMake files and tests
are kept with the snapshot for provenance.

Build integration: `vendor/CMakeLists.txt` exposes the header through the
`vendor_rang` interface target for terminal color and pty-aware dump formatting.

Local modifications: none.
