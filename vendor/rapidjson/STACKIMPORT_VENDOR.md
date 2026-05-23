# RapidJSON vendoring notes

Source: https://github.com/Tencent/rapidjson

Version: v1.1.0 release archive

License: MIT. See `license.txt`.

Scope: this repo vendors only `include/rapidjson` plus upstream metadata files.
The full upstream archive includes sample/tool directories that are not needed by
stackimport and are intentionally excluded.

Build integration: `vendor/CMakeLists.txt` exposes the headers through the
`vendor_rapidjson` interface target. stackimport wraps RapidJSON allocation with
`StackImportRapidJsonAllocator` so JSON generation uses the caller-injected
platform allocator.
