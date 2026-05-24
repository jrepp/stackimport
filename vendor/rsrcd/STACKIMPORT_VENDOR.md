# rsrcd - C++ Resource Fork Parser

This is a derivative work from [rsrcdump](https://github.com/jorio/rsrcdump) by
Iliyas Jorio, implemented as a small C++23 header-only parser.

## Origin

rsrcdump parses Classic Mac OS resource forks and converts common resource types
to modern formats. This local header keeps the resource fork parsing pieces
available to stackimport without adding a runtime dependency.

## Local Integration

- **Target**: `vendor_rsrcd` (interface library)
- **Language**: C++23
- **Dependencies**: none required by the current header-only integration
- **Build**: CMake with C++23 and strict warnings

## Local Modifications

- Header-only C++23 parser interface using non-owning byte views
- Explicit reader/writer/allocator hooks for integration with stackimport
- CMake build system with stackimport's standard flags

## License

MIT License - see [LICENSE](LICENSE)
