# stackimport vendoring note

This directory is a vendored copy of the former `snd2wav` git submodule.

- Upstream URL: `https://github.com/uliwitness/snd2wav.git`
- Vendored commit: `d6900ad35ba4da1fb488fe006f64a7977d98b32c`
- Local integration: built by `vendor_snd2wav` in `vendor/CMakeLists.txt`
- License note: no explicit license file was present in the snapshot; source
  headers include copyright notices stating all rights reserved.

No upstream source changes are intended in this directory. Prefer integration
changes in the CMake wrapper or stackimport call sites.
