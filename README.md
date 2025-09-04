<p align="center">
  <img src="https://raw.githubusercontent.com/flatpak/flatpak/main/flatpak.png" alt="Flatpak icon"/>
</p>

`flatpak-builder` is a tool for building [flatpaks](https://flatpak.org) from sources.

It reads a JSON or YAML based manifest to automatically download, build, and install projects which eventually get exported into a flatpak.

For information on the manifest format see `man flatpak-manifest`. A JSON Schema for this format is [available here](https://github.com/flatpak/flatpak-builder/blob/main/data/flatpak-manifest.schema.json).

To use the JSON schema, in [an editor with support](https://code.visualstudio.com/docs/languages/json) for schemas, you can include this line in your manifest: 

```json
  "$schema": "https://raw.githubusercontent.com/flatpak/flatpak-builder/main/data/flatpak-manifest.schema.json"
```

For information on the command-line tool see `man flatpak-builder` or the [online documentation](https://docs.flatpak.org/en/latest/flatpak-builder-command-reference.html).

# Installation

Flatpak-builder uses the [Meson build system](https://mesonbuild.com/). To build just do:
```sh
 meson setup _build
 meson install -C _build
```

Configure arguments are documented in `meson_options.txt`.

## Runtime dependencies

The `flatpak-builder` tool requires `flatpak` being available on the host to
function. Depending on the manifest used it also requires some commands be available on
the host.

Very commonly used:

 * sh
 * patch
 * tar
 * cp
 * git
 * 7z
 * bsdunzip (libarchive)
 * git-lfs

Rarely used:

 * rpm2cpio & cpio
 * svn
 * bzr
