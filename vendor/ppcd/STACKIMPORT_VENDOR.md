# ppcd Vendor Snapshot

- Upstream: https://github.com/ogamespec/ppcd
- Snapshot commit: `58f7e6df284fa4cd5a9a734b7a44d1851dcdaf16`
- Retrieved: 2026-05-23
- License: CC0-1.0, see `LICENSE`
- Local integration: `vendor_ppcd` static library target in `vendor/CMakeLists.txt`
- Included upstream files: `ppcd.cpp`, `ppcd.h`, `CommonDefs.h` contents stored as `Commondefs.h`, `README.md`, `LICENSE`
- Local source modifications: one const-correctness fix in `ppcd.cpp`
  changes a local string-literal pointer from `char *` to `const char *` so the
  snapshot builds with modern C++ compilers. The upstream `CommonDefs.h` file is
  stored as `Commondefs.h` because upstream `ppcd.cpp` includes `Commondefs.h`;
  this keeps the snapshot buildable on case-sensitive filesystems.

## Checksums

SHA-256 checksums of included upstream files at the snapshot commit:

| File | SHA-256 |
| --- | --- |
| `ppcd.cpp` | `2ac58f015f582e1168aa8716d203e56912c697b77f25f44c658311b21a3e8c3f` |
| `ppcd.h` | `dd50e835b86ade0a0af7ce559f47d2f755880812aa97a98c9661d8c2dd17ed09` |
| `CommonDefs.h` | `0de207d099b5abecf9cb55477bcf267491ad8e15b999b081b5cdc27a72ad0e6d` |
| `LICENSE` | `a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499` |
| `README.md` | `ff5468af76b75a9aab1968a9011af5e06c16b9331d8639223d8f2e172ea2dbe3` |

## Updating

Use an exact commit SHA for reproducible updates:

```sh
SHA=<new-upstream-commit>
gh api -H "Accept: application/vnd.github.raw" "/repos/ogamespec/ppcd/contents/ppcd.cpp?ref=${SHA}" > vendor/ppcd/ppcd.cpp
gh api -H "Accept: application/vnd.github.raw" "/repos/ogamespec/ppcd/contents/ppcd.h?ref=${SHA}" > vendor/ppcd/ppcd.h
gh api -H "Accept: application/vnd.github.raw" "/repos/ogamespec/ppcd/contents/CommonDefs.h?ref=${SHA}" > vendor/ppcd/Commondefs.h
gh api -H "Accept: application/vnd.github.raw" "/repos/ogamespec/ppcd/contents/README.md?ref=${SHA}" > vendor/ppcd/README.md
gh api -H "Accept: application/vnd.github.raw" "/repos/ogamespec/ppcd/contents/LICENSE?ref=${SHA}" > vendor/ppcd/LICENSE
```

After updating, refresh this file's snapshot commit, retrieval date, checksum table, and `vendor/INDEX.md` entry in the same change. Verify with:

```sh
cmake -S . -B build
cmake --build build --target vendor_ppcd
```
