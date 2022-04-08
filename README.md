<p align="center">
  <img src="https://raw.githubusercontent.com/flatpak/flatpak/main/flatpak.png" alt="Flatpak icon"/>
</p>

Flatpak-builder is a tool for building flatpaks from sources.

See http://flatpak.org/ for more information.

Read documentation for the flatpak-builder [commandline tools](http://docs.flatpak.org/en/latest/flatpak-builder-command-reference.html).

# INSTALLATION

Flatpak-builder uses a traditional autoconf-style build mechanism. To build just do
```
 ./configure [args]
 make
 make install
```

Most configure arguments are documented in `./configure --help`. However,
there are some options that are a bit more complicated.

Flatpak-builder relies on flatpak, so it must be installed first.
