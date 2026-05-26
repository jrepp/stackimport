# Release Process

`stackimport` uses Semantic Versioning for release tags and artifacts.

## Version Schema

The canonical version lives in `VERSION.txt` and must be a valid SemVer 2.0.0
version:

```text
MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]
```

- Increment `MAJOR` for incompatible CLI, output-package, or public C API
  contract changes.
- Increment `MINOR` for backwards-compatible importer, report, disassembly, or
  API additions.
- Increment `PATCH` for backwards-compatible bug fixes and corpus coverage fixes.
- Use prerelease suffixes such as `0.2.0-alpha.1` for unstable cuts.

The public C ABI version in `STACKIMPORT_API_VERSION` is separate. Bump it when
the C API struct/function contract changes, then choose the matching SemVer bump
above.

## Git Tags

Release tags must be `v` followed by the exact `VERSION.txt` value:

```sh
git tag v0.1.0
git push origin v0.1.0
```

The release workflow rejects tags that do not match `VERSION.txt`.

## Automatic Patch Releases

Use the `Patch Release` workflow to generate the next patch number
automatically. It reads the current stable `VERSION.txt`, inspects existing
`vMAJOR.MINOR.PATCH` tags for that same major/minor line, writes the next patch
number to `VERSION.txt`, commits the bump, tags it, pushes both the branch and
tag, and dispatches the normal `Release` workflow from that tag.

For example, if `VERSION.txt` is `0.1.0` and the highest existing `v0.1.*` tag
is `v0.1.3`, the workflow creates:

```text
VERSION.txt = 0.1.4
tag = v0.1.4
```

The patch workflow then starts the normal `Release` workflow.

## GitHub Release Workflow

`.github/workflows/release.yml` runs for `v*` tags and can also be started
manually with a `version` input. It:

1. Validates `VERSION.txt`, the requested version, and the tag name.
2. Builds and tests the same six platform targets as the build workflow.
3. Packages the CLI, test binary, shared C ABI library, combined static library,
   public header, `VERSION.txt`, and `README.md` into per-platform zip archives.
   macOS artifacts also include `StackImport.framework`.
4. Publishes a GitHub release and uploads those archives.
5. For stable releases, updates the `jrepp/homebrew-stackimport` tap formula
   after the GitHub release is published successfully.

Prerelease SemVer values containing `-`, such as `0.2.0-beta.1`, are published
as GitHub prereleases and do not update the Homebrew tap.

## Homebrew Tap Automation

The tap repository is `jrepp/homebrew-stackimport`. The release workflow renders
`Formula/stackimport.rb` with `scripts/render_homebrew_formula.py`, using the
release tag's GitHub source archive and its SHA-256 hash.

The formula builds from source with the default vendored conversion-tool support
enabled. It installs the CLI, shared dylib, combined static archive, public C
header, `pkg-config` file, and on macOS `StackImport.framework`.

Cross-repository writes use the `HOMEBREW_TAP_DEPLOY_KEY` Actions secret on
`jrepp/stackimport`. The matching public deploy key must be installed on
`jrepp/homebrew-stackimport` with write access. If the key is rotated, replace
both the source-repository secret and the tap-repository deploy key before the
next stable release.

After a stable release completes, users can install with:

```sh
brew tap jrepp/stackimport
brew install stackimport
```
