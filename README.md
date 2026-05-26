stackimport
===========

A command line tool that reads a HyperCard stack and generates a folder with JSON and media files from it containing
a more easily readable representation of its contents. Based on Tyler Vano's and Rebecca Bettencourt's code from
http://creysoft.com/xtalk/


Install With Homebrew
---------------------

Stable releases are available from the `jrepp/stackimport` Homebrew tap:

    brew tap jrepp/stackimport
    brew install stackimport

The formula installs the `stackimport` CLI, the public C header, the shared C ABI
library, the static archive, and `pkg-config` metadata for embedders:

    pkg-config --cflags --libs stackimport


How to build this
-----------------

The project builds with CMake and requires a C17/C++23-capable compiler:

    cmake -S . -B build
    cmake --build build
    ctest --test-dir build --output-on-failure

The compatibility Makefile wraps the same CMake flow:

    make stackimport
    make test

The CMake build defines `MAC_CODE=0`, so it builds the portable stack parser without the old Mac resource-manager
path. For full resource-fork functionality, you'll also have to write code to replace the Mac-specific code that
extracts resources from the resource fork of the stack and converts 'snd ' resources to AIFF. The resource-fork-reading
code in https://github.com/uliwitness/reclassicfication may be a good starting point.

Strict compiler warnings are enabled by default. To make warnings fail the build:

    cmake -S . -B build -DSTACKIMPORT_WARNINGS_AS_ERRORS=ON

Release versions use Semantic Versioning. The canonical release version lives in
`VERSION.txt`, release tags are `v<VERSION>`, and the release workflow validates that
the tag and file match before publishing artifacts. Patch release numbers can be
generated automatically with the `Patch Release` workflow. See `RELEASE.md`.

Static-analysis hooks are available through CMake when the local tools are
installed:

    cmake -S . -B build-analysis \
      -DSTACKIMPORT_ENABLE_CLANG_TIDY=ON \
      -DSTACKIMPORT_ENABLE_CLANG_STATIC_ANALYZER=ON \
      -DSTACKIMPORT_ENABLE_CPPCHECK=ON \
      -DSTACKIMPORT_ENABLE_INCLUDE_WHAT_YOU_USE=ON
    cmake --build build-analysis

Vendored conversion tools are built on a separate path and inherit the same
C17/C++23 policy where their build systems allow it:

    cmake --build build --target vendor-tools --parallel

Format References
-----------------

The HyperCard stack format was not officially published, so parser work is backed
by local notes, imported references, and corpus runs. The maintained index is
`Stack File Format/README.md`; start there before changing parser behavior.

Current reference material includes:

- `Stack File Format/*/*.txt` notes copied from the older xtalk codebase.
- Creysoft's HyperCard file format write-up:
  https://creysoft.com/xtalk/hypercard_file_format2.htm
- Rebecca Bettencourt's 2011 HyperCard file format guide, normalized into
  `Stack File Format/DefinitiveGuide2011.md`.
- `Stack File Format/AddColor.md` for AddColor resource notes.
- `Stack File Format/ConversionCorpusGrounding.md` for conversion provenance,
  corpus indexing, and preferred modern output formats.
- Parser behavior and exported JSON/media evidence in `CStackFile.cpp`, corpus
  run reports, and generated `.xstk` packages.

When parser behavior changes, update the relevant format notes in the same change
so block/resource coverage, confidence level, unknown bytes, and corpus evidence
stay aligned with the implementation.

Vendored Libraries
------------------

Third-party source snapshots live under `vendor/`. `vendor/INDEX.md` is the
source-of-truth dependency index, including license paths, local integration
targets, and local modification notes. Preserve upstream license files and update
that index whenever a vendored dependency is added, removed, upgraded, or patched.

The main build currently uses these vendored libraries:

- `rsrcd/` - header-only Classic Mac resource fork parser derived from
  `rsrcdump`; MIT; used through `vendor_rsrcd`.
- `quill/` - Quill logging headers used by stackimport diagnostics; MIT; used
  through `vendor_quill`.
- `rang/` - terminal color support for log/dump formatting; Unlicense; used
  through `vendor_rang`.
- `rapidjson/` - JSON DOM/writer used for `.xstk` metadata; MIT; used through
  `vendor_rapidjson`.
- `stb/` - `stb_image_write.h` for PNG output; MIT or public domain; used
  through `vendor_stb_image_write`.
- `dr_wav/` - single-header WAV reader/writer candidate retained for comparison;
  public domain or MIT-0; linked into the core importer through `vendor_dr_wav`.
- `snd2wav/` - legacy Mac `snd ` to WAV helper snapshot retained as reference
  material; built as `vendor_snd2wav`; see `vendor/INDEX.md` for its licensing
  caveat; linked into the core importer.
- `ppcd/` - PowerPC disassembler snapshot for PPC code-resource work; CC0-1.0;
  built as `vendor_ppcd` and linked into the core importer.

Vendored conversion tools also build Deark, `resource_dasm`, and `phosg` for
resource conversion and reverse-engineering work. These tools are part of the
normal StackImport build; unused vendored dependencies should be removed rather
than compiled out.

Embedding API
-------------

`stackimport_c.h` exposes a stable C ABI for C and C++ callers. Use one
`stackimport_context` per concurrent import. The preferred ownership model is
caller-owned storage:

    _Alignas(max_align_t) unsigned char storage[4096];
    stackimport_context *ctx = NULL;
    stackimport_context_init(storage, sizeof(storage), &ctx);

Callers that prefer heap allocation can provide `stackimport_allocator` callbacks
to `stackimport_context_create`. Initialize public structs with
`stackimport_allocator_init` and `stackimport_import_options_init` before setting
fields; this keeps callers source-compatible as new optional fields are added.
Callers that need an explicit non-default calling convention can define
`STACKIMPORT_CALL` before including `stackimport_c.h`; the macro is applied to
both exported functions and callback typedefs.

For embedders that need full control over resource acquisition, use
`stackimport_platform` and create contexts with
`stackimport_context_create_with_platform` or
`stackimport_context_init_with_platform`. The platform object is copied into the
context at setup time and supplies allocation, diagnostics, output-file writes,
file close, and directory creation. The CLI is just one caller of that interface;
it injects libc-backed implementations.

The API does not return owned strings or buffers, and it does not derive output
locations. Embedders must pass explicit input and output package paths in
`stackimport_import_options`; process policy such as relative path resolution,
default `.xstk` naming, logging flags, and thread scheduling belongs in the
caller. Unknown import flags fail with `STACKIMPORT_STATUS_UNSUPPORTED_OPTION`
instead of being silently ignored.

Vendored converters should only be called from stackimport adapter code that has
access to this same platform object. Avoid calling vendor APIs directly from the
CLI or parser when adding new conversion paths; wrap them behind a stackimport
adapter so allocation, filesystem, diagnostics, and threading policy remain
caller-injected.

Current limitation: the public C boundary is allocation-explicit, but the legacy
parser implementation behind it still uses C++ standard-library containers
internally. Treat contexts as independent and do not share one context across
threads without external synchronization.


How to use this
---------------

Once you have built the stackimport command line tool, just use it. Syntax is

    stackimport [--nodumprawblocks] [--dumprawblocks] [--nostatus] [--noprogress] [--rawgraphics] [--output <packagePath>] <originalStackPath>

where originalStackPath is the HyperCard stack you want to convert.
If `--output` is omitted, stackimport creates a sibling `.xstk` directory next
to the input stack, replacing the input extension when one is present.

Where the options are:

dumprawblocks - Create files containing the raw data from each block in the stack.
This is enabled by default so a normal run preserves evidence for structures the
parser does not fully understand yet.

nodumprawblocks - Do not write per-block raw `.data` files. This is useful for a
small output package when you only need decoded JSON/media.

nostatus - Don't output status messages while converting the file. These are mostly useful
if you are displaying a progress UI, or if conversion aborts and you need to know what bock
caused the error.

noprogress - Do not show 'Progress 1 of 5' etc. messages. These messages are useful for
updating the progress bar in a progress UI.

High-level CLI example:

    cmake --build build --target stackimport
    build/stackimport --output /tmp/MyStack.xstk /path/to/MyStack

The output package contains generated JSON metadata, decoded media, and raw block
data by default.

Installable build:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    cmake --install build --prefix /usr/local

The install step writes `bin/stackimport`, `lib/libstackimport_c.*`,
`lib/libstackimport.a`, `include/stackimport_c.h`, and
`lib/pkgconfig/stackimport.pc`. On macOS it also installs
`Frameworks/StackImport.framework`. A Homebrew tap can wrap the same flow in a
formula; consumers can discover the C API flags with
`pkg-config --cflags --libs stackimport` after installing the formula. Use the
shared C ABI library (`stackimport_c`) when embedding from C, Swift, Python,
Ruby, or another host that should not inherit C++ link/runtime details from the
static archive.

Useful release build targets:

    cmake --build build --target stackimport-release
    cmake --build build --target stackimport-release-package

On macOS, the release aggregate includes the versioned
`libstackimport_c.dylib`, the combined static archive, and
`StackImport.framework`. The deployment target defaults to macOS 15.0 unless
`MACOSX_DEPLOYMENT_TARGET` or `CMAKE_OSX_DEPLOYMENT_TARGET` is set explicitly.

Stable releases update the `jrepp/homebrew-stackimport` tap automatically. After
a release completes, install with:

    brew tap jrepp/stackimport
    brew install stackimport

Minimal tap formula shape:

```ruby
class Stackimport < Formula
  desc "HyperCard stack importer"
  homepage "https://github.com/<owner>/stackimport"
  url "https://github.com/<owner>/stackimport/archive/refs/tags/v1.2.3.tar.gz"
  sha256 "<sha256>"
  license "MIT"

  depends_on "cmake" => :build

  def install
    system "cmake", "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release",
                    "-DSTACKIMPORT_BUILD_TESTS=OFF",
                    "-DSTACKIMPORT_BUILD_VENDOR_TESTS=OFF",
                    "-DCMAKE_INSTALL_PREFIX=#{prefix}"
    system "cmake", "--build", "build", "--target", "install"
  end

  test do
    (testpath/"smoke.c").write <<~C
      #include <stackimport_c.h>
      int main(void) {
        return stackimport_api_version() == STACKIMPORT_API_VERSION ? 0 : 1;
      }
    C
    system ENV.cc, "smoke.c", "-I#{include}", "-L#{lib}",
                   "-lstackimport_c", "-Wl,-rpath,#{lib}", "-o", "smoke"
    system "./smoke"
  end
end
```

Corpus import runner:

    scripts/import_all_stacks.py

The runner has a `#!/usr/bin/env -S uv run --script` shebang, so it can be run
directly from the repository root. It scans the default Pantechnicon mirror,
extracts `.sit`/`.hqx` archives, imports files classified as stacks, and writes
reports under `import-runs/<run-id>/`.

High-level C API example:

    #include "stackimport_c.h"
    #include <stddef.h>
    #include <stdio.h>

    int main(void) {
        _Alignas(max_align_t) unsigned char storage[4096];
        stackimport_context *ctx = NULL;

        stackimport_status status =
            stackimport_context_init(storage, sizeof(storage), &ctx);
        if (status != STACKIMPORT_STATUS_OK) {
            fprintf(stderr, "stackimport init failed: %s\n",
                    stackimport_status_string(status));
            return 1;
        }

        stackimport_import_options options;
        stackimport_import_options_init(&options);
        options.input_path = "/path/to/MyStack";
        options.output_package_path = "/tmp/MyStack.xstk";
        options.flags = STACKIMPORT_IMPORT_NO_STATUS |
                        STACKIMPORT_IMPORT_NO_PROGRESS;

        status = stackimport_import(ctx, &options);
        stackimport_context_deinit(ctx);

        if (status != STACKIMPORT_STATUS_OK) {
            fprintf(stderr, "stackimport failed: %s\n",
                    stackimport_status_string(status));
            return 1;
        }
        return 0;
    }

For production embedding, provide a `stackimport_platform` when constructing the
context if you need caller-owned allocation, diagnostics, filesystem, or
threading policy.


License
-------

    Copyright (c) 2005-2026 Rebecca Bettencourt, Uli Kusterer (Mr. Z),
    Jacob Repp, and contributors

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    
    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.
    
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
