# Quill vendoring notes

Source: https://github.com/odygrd/quill

Version: v11.1.0 release archive

License: MIT. See `LICENSE`.

Scope: this repo vendors the upstream source archive intact so Quill's headers,
documentation, tests, and bundled third-party notices remain available for audit.

Build integration: `vendor/CMakeLists.txt` exposes Quill through the
`vendor_quill` interface target. The wrapper uses Quill as a header-only library,
links `Threads::Threads`, defines `QUILL_NO_EXCEPTIONS` to match stackimport's
exception-free build, and defines `QUILL_DISABLE_NON_PREFIXED_MACROS` to keep the
global macro namespace clean.

Local modifications: none.
