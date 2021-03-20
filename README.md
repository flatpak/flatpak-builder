<p align="center">
  <img src="https://raw.githubusercontent.com/flatpak/flatpak/main/flatpak.png" alt="Flatpak icon"/>
</p>

Flatpak-builder is a tool for building flatpaks from sources.

See http://flatpak.org/ for more information.

Read documentation for the flatpak-builder [commandline tools](http://docs.flatpak.org/en/latest/flatpak-builder-command-reference.html).

# Installation

Flatpak-builder uses a the Meson build system. To build just do:
```
 ./meson . _build [args]
 ninja -C _build
 ninja -C _build install
```

Configure arguments are documented in `meson_options.txt`.

Flatpak-builder depends on the following executables being present in the
host system:

 * tar
 * unzip
 * sh
 * patch
 * cp

Optionally, flatpak-builder may try to execute the following programs if the
manifest file requires:

 * bzr
 * git
 * rpm2cpio & cpio
 * svn
 * 7z

Flatpak-builder relies on flatpak, so it must be installed first.
