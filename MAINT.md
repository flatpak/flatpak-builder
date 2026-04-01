## Maintainance notes

- Please see the [versioning policy](https://github.com/flatpak/flatpak-builder/tree/main?tab=readme-ov-file#supported-versions).
- Stable branches will only get bugfixes and non-breaking enhancements.
- Everything is merged to `main` and from time to time suitable changes
  are backported to the stable `flatpak-builder-1.EVEN_MINOR.x`
  branches. GitHub milestone can be used for tracking which PRs should
  be backported.
- Stable tags are created from `flatpak-builder-1.EVEN_MINOR.x` branch.
- Unstable tags are created from the `main` branch.
- The first release of a new stable release line eg. `1.6.0`
  is tagged from `main`. After that the `flatpak-builder-1.6.x` branch
  is to be created and the `1.6.1` is made from that branch. During this
  time `main` should not receive breaking changes until the new
  stable branch is created.

## Releasing

- Update the `NEWS`
- Update version number in `meson.build` and `configure.ac`.
- Open a PR titled "Release $VERSION" with the above changes.
- Build with `meson` and `make`, with all options enabled, locally.
- Once CI on PR and local builds are successful merge the PR.

## Tagging

- Checkout the target branch and pull the latest changes from above.
- Create a signed and annotated tag using either git or git-evtag.

```
git tag -s -m "<MAJOR.MINOR.PATCH>" <MAJOR.MINOR.PATCH>
```

NOTE. The tags are created in the `MAJOR.MINOR.PATCH` format eg.
`1.4.6` (WITHOUT the `v*` prefix).

- Push the tag.

```
git push origin $TAG
```

Once the tag is pushed, the GitHub release workflow will rebuild and
run tests on it. If successful, it will then automatically create a
GitHub release from the tag with the dist tarballs attached.
