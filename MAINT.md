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
- Open a  PR titled "Release $VERSION" with the above to see if CI passes.
- Merge the PR to the target branch.
- Check out the target branch, pull the above change locally and make
  sure the submodules are correct and checked out.
- Build with `meson` and `make`, with all options enabled.
- Create a tarball with `make dist`. The tarball is created from a
  clean checkout. It is produced as `flatpak-builder-$VERSION.tar.xz`.
- Verify the project is buildable using the tarball. The tarball MUST
  contain the submodule files.

## Tagging

- The tags are created in the `MAJOR.MINOR.PATCH` format eg. `1.4.6`
  (WITHOUT the `v*` prefix).
- Ideally the tags should be signed and annotated tags. Optionally
  git-evtag can be used.
- The tag message should have the changelog and the checksum of the
  tarball that will be attached to GitHub releases.
- Once the tag is pushed, a GitHub release from that tag is to be
  created.

  The release tag is the new tag, the title is `VERSION` and the release
  body is the message from the tag. Additional notifications and details
  can be documented to the release body.

  Then the `flatpak-builder-$VERSION.tar.xz` tarball is to be attahced.
  This is the primary way downstreams consume Flatpak builder.


NOTE: GitHub releases are set as _immutable_, so please be careful.
