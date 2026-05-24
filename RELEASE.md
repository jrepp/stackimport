# Release Process

`stackimport` uses Semantic Versioning for release tags and artifacts.

## Version Schema

The canonical version lives in `VERSION` and must be a valid SemVer 2.0.0
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

Release tags must be `v` followed by the exact `VERSION` value:

```sh
git tag v0.1.0
git push origin v0.1.0
```

The release workflow rejects tags that do not match `VERSION`.

## GitHub Release Workflow

`.github/workflows/release.yml` runs for `v*` tags and can also be started
manually with a `version` input. It:

1. Validates `VERSION`, the requested version, and the tag name.
2. Builds and tests the same six platform targets as the build workflow.
3. Packages the CLI, test binary, static library, public header, `VERSION`, and
   `README.md` into per-platform zip archives.
4. Publishes a GitHub release and uploads those archives.

Prerelease SemVer values containing `-`, such as `0.2.0-beta.1`, are published
as GitHub prereleases.
