sources = files([
  'builder-cache.c',
  'builder-context.c',
  'builder-extension.c',
  'builder-flatpak-utils.c',
  'builder-git.c',
  'builder-main.c',
  'builder-manifest.c',
  'builder-module.c',
  'builder-options.c',
  'builder-post-process.c',
  'builder-sdk-config.c',
  'builder-source-archive.c',
  'builder-source-bzr.c',
  'builder-source.c',
  'builder-source-dir.c',
  'builder-source-extra-data.c',
  'builder-source-file.c',
  'builder-source-git.c',
  'builder-source-patch.c',
  'builder-source-script.c',
  'builder-source-shell.c',
  'builder-source-svn.c',
  'builder-utils.c',
])

# Those are the minimum set of common programs that makes flatpak-builder actually useful
required_programs = [
  'git',
  'tar',
  'unzip',
  'sh',
  'patch',
  'cp',
]
foreach program : required_programs
  find_program(program)
endforeach

# Less frequently useful programs
optional_programs = [
  'rpm2cpio',
  'cpio',
  'svn',
  '7z',
]
foreach program : optional_programs
  find_program(program, required: false)
endforeach
find_program('brz', 'bzr', required: false)

executable(
  meson.project_name(),
  sources,
  dependencies: flatpak_builder_deps,
  include_directories: inc_dirs,
  install: true,
)

