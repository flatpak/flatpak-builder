project(
  'flatpak-builder',
  'c',
  license: 'LGPL-2.1-or-later',
  meson_version: '>=0.49',
  version: '1.0.11',
  default_options: 'c_std=gnu99',
)

# TODO: do some cleaning up of useless args that meson already sets by default
add_project_arguments(
  '-Werror=empty-body',
  '-Werror=strict-prototypes',
  '-Werror=missing-prototypes',
  '-Werror=implicit-function-declaration',
  '-Werror=format=2',
  '-Werror=format-security',
  '-Werror=format-nonliteral',
  '-Werror=pointer-arith',
  '-Werror=init-self',
  '-Werror=missing-declarations',
  '-Werror=return-type',
  '-Werror=overflow',
  '-Werror=int-conversion',
  '-Werror=parentheses',
  '-Werror=incompatible-pointer-types',
  '-Werror=misleading-indentation',
  '-Werror=missing-include-dirs',
  # Necessary for non POSIX stuff in glibc like fallocate
  '-D_GNU_SOURCE',
  language: 'c',
)

inc_dirs = include_directories(
  '.',
# FIXME: handle $prefixes/libdwarf/ too or let it to the option? The later would be easier
  # Dwarf header stuff
  get_option('dwarf_header_dir'),
)

cc = meson.get_compiler('c')

# if not cc.has_header('sys/xattr.h')
  # error('You must have the sys/xattr.h header from glibc, needed for libglnx')
# endif

if not cc.has_header('dwarf.h', include_directories: inc_dirs)
  error('dwarf.h header could not be found in either the include directories or @0@'
    .format(get_option('dwarf_header_dir'))
  )
endif

libglnx_dep = subproject('libglnx').get_variable('libglnx_dep')
libyaml_dep = dependency('yaml-0.1', required: get_option('yaml'))
libelf_dep = dependency('libelf', version: '>=0.8.12')

glib_req = '>=2.44'
flatpak_builder_deps = [
  libyaml_dep,
  libglnx_dep,
  libelf_dep,
  dependency('glib-2.0', version: glib_req),
  dependency('gio-2.0', version: glib_req),
  dependency('gio-unix-2.0', version: glib_req),
  dependency('libsoup-2.4'),
  dependency('ostree-1', version: '>=2017.14'),
  dependency('json-glib-1.0'),
  dependency('libxml-2.0', version: '>=2.4'),
  dependency('libcurl'),
  # dependency(''),
]
# find_program('flatpak', version: '>=0.99.1')

config_data = configuration_data()
config_data.set10('FLATPAK_BUILDER_ENABLE_YAML', true)
config_data.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_data.set_quoted('PACKAGE_VERSION', meson.project_version())
config_data.set_quoted('PACKAGE_STRING', '@0@-@1@'.format(meson.project_name(), meson.project_version()))
config_h = configure_file(
  configuration: config_data,
  output: 'config.h',
)

subdir('src')
if get_option('docs')
  subdir('doc')
endif
if get_option('tests')
  subdir('tests')
endif
