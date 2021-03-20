xsltproc_args = [
  '--nonet',
  '--stringparam', 'man.output.quietly', '1',
  '--stringparam', 'funcsynopsis.style', 'ansi',
  '--stringparam', 'man.th.extra1.suppress', '1',
  '--stringparam', 'man.authors.section.enabled', '0',
  '--stringparam', 'man.copyright.section.enabled', '0',
]
xsltproc = find_program('xsltproc')
xmlto = find_program('xmlto')

# TODO: is the docbook DTD/XSL catalog check needed or does xsltproc take care of that?
configured_xml = configure_file(
  configuration: {
    'VERSION': meson.project_version(),
    # FIXME: that's not a good way of this it…
    'srcdir': meson.current_source_dir(),
  },
  input: 'flatpak-builder-docs.xml.in',
  # depend_files:
  output: 'flatpak-builder-docs.xml',
)

man_gen_command = [
  xsltproc, xsltproc_args,
  'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
]

man_pages = [
  ['flatpak-builder', '1'],
  ['flatpak-manifest', '5'],
]
foreach man_page : man_pages
  man_page_name = '@0@.@1@'.format(man_page[0], man_page[1])
  custom_target(man_page[0] + '-man',
    command: [man_gen_command, man_page_name],
    depend_files: man_page[0] + '.xml',
    output: man_page_name,
    install: true,
    install_dir: get_option('mandir') / 'man' + man_page[1],
  )
endforeach

doc_dir = get_option('datadir') / 'doc' / meson.project_name()
html = custom_target('docbook',
  command: [
    xmlto,
    '--skip-validation',
    'xhtml-nochunks',
    '-o', meson.current_build_dir(),
    '-m', files('xmlto-config.xsl'),
    '@INPUT@',
  ],
  input: configured_xml,
  output: 'flatpak-builder-docs.html',
  install: true,
  install_dir: doc_dir,
)

install_data(
  files('docbook.css'),
  install_dir: doc_dir,
)
