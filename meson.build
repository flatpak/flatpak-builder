project(
  'flatpak-builder',
  'c',
  license: 'LGPL-2.1-or-later',
  meson_version: '>= 0.56.2',
  version: '1.4.0',
  default_options: 'c_std=gnu99',
)

cc = meson.get_compiler('c')

project_c_args = [
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
]

foreach arg : project_c_args
  if cc.has_argument(arg)
    add_project_arguments(arg, language: 'c')
  endif
endforeach

# The debugedit program is a hard dependency
debugedit = find_program('debugedit', version: '>= 5.0')

# Require appstream with compose plugin installed
appstreamcli = find_program('appstreamcli', version: '>= 0.16.3')
appstreamcli_compose = run_command(appstreamcli, ['compose', '--help'], check: true)

subdir('src')
subdir('doc')
if get_option('tests')
  subdir('tests')
endif
