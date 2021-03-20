flatpak_builder_sources = files(
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
  'builder-source.c',
  'builder-source-archive.c',
  'builder-source-bzr.c',
  'builder-source-dir.c',
  'builder-source-extra-data.c',
  'builder-source-file.c',
  'builder-source-git.c',
  'builder-source-inline.c',
  'builder-source-patch.c',
  'builder-source-script.c',
  'builder-source-shell.c',
  'builder-source-svn.c',
  'builder-utils.c',
)

config_data = configuration_data()
config_data.set10('FLATPAK_BUILDER_ENABLE_YAML', true)
config_data.set_quoted('DEBUGEDIT', debugedit.full_path())
config_data.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_data.set_quoted('PACKAGE_VERSION', meson.project_version())
config_data.set_quoted('PACKAGE_STRING', '@0@-@1@'.format(meson.project_name(), meson.project_version()))
config_h = configure_file(
  configuration: config_data,
  output: 'config.h',
)

glib_req = '>= 2.44'
flatpak_builder_deps = [
  dependency('glib-2.0', version: glib_req),
  dependency('gio-2.0', version: glib_req),
  dependency('gio-unix-2.0', version: glib_req),
  dependency('json-glib-1.0'),
  dependency('libcurl'),
  dependency('libelf', version: '>= 0.8.12'),
  dependency('libglnx', default_options: ['tests=false']),
  dependency('libsoup-2.4'),
  dependency('libxml-2.0', version: '>= 2.4'),
  dependency('ostree-1', version: '>= 2017.14'),
  dependency('yaml-0.1', required: get_option('yaml')),
]

flatpak_builder = executable(
  'flatpak-builder',
  flatpak_builder_sources,
  dependencies: flatpak_builder_deps,
  install: true,
)

