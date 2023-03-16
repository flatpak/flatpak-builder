xsltproc = find_program('xsltproc', required: get_option('docs'))
xmlto = find_program('xmlto', required: get_option('docs'))

if xmlto.found() and xsltproc.found()

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

  man_pages = [
    ['flatpak-builder', '1'],
    ['flatpak-manifest', '5'],
  ]
  foreach man_page : man_pages
    man_page_name = '@0@.@1@'.format(man_page[0], man_page[1])
    custom_target(man_page[0] + '-man',
      command: [
        xsltproc,
        '--nonet',
        '--stringparam', 'man.output.quietly', '1',
        '--stringparam', 'funcsynopsis.style', 'ansi',
        '--stringparam', 'man.th.extra1.suppress', '1',
        '--stringparam', 'man.authors.section.enabled', '0',
        '--stringparam', 'man.copyright.section.enabled', '0',
        '--output', '@OUTPUT@',
        'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
        '@INPUT@'
      ],
      input: man_page[0] + '.xml',
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

endif
