# CLI11 vendoring notes

Source: https://github.com/CLIUtils/CLI11

Version: v2.6.2 release asset `CLI11.hpp`

License: BSD-3-Clause. See `LICENSE`.

Scope: this repo vendors the generated single-header release asset at
`include/CLI/CLI.hpp` plus the upstream license. The full upstream source tree,
tests, examples, and documentation are intentionally excluded.

Provenance:

- Release page: https://github.com/CLIUtils/CLI11/releases/tag/v2.6.2
- Header URL: https://github.com/CLIUtils/CLI11/releases/download/v2.6.2/CLI11.hpp
- Header SHA-256:
  `227a16fe5f9f8ada80c3c409492475536f597e7bd83a6c26eacc3c8c149a9295`

Build integration: `vendor/CMakeLists.txt` exposes the headers through the
`vendor_cli11` interface target. StackImport uses CLI11 only in the command-line
executable, not in the core C ABI or shared library.

Local modifications: none.
