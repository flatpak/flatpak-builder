installed_testdir = get_option('prefix') / get_option('libexecdir') / 'installed-tests' / 'flatpak-builder'
installed_tests_metadir = get_option('prefix') / get_option('datadir') / 'installed-tests' / 'flatpak-builder'

test_env = environment()
test_env.set('FLATPAK_TESTS_DEBUG', '1')
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
test_env.prepend('PATH', meson.current_build_dir() / '..' / 'src', separator: ':')

test_names = [
  'test-builder',
  'test-builder-deprecated',
  'test-builder-python',
]

tap_test = find_program(
  files(meson.project_source_root() / 'buildutil/tap-test'),
)

foreach test_name : test_names
  filename = test_name + '.sh'

  test(
    test_name,
    tap_test,
    args: [files(filename)],
    env: test_env,
    depends: flatpak_builder,
    protocol: 'tap',
    workdir: meson.current_build_dir(),
  )

  if get_option('installed_tests')
    install_data(
      filename,
      install_dir : installed_testdir,
      install_mode : 'rwxr-xr-x',
    )
    configure_file(
      input: 'tap.test.in',
      output: filename + '.test',
      configuration: {
        # After dropping Autotools we could use test_name here (avoiding the
        # redudant .sh), but for now include the .sh to keep the installed
        # files easier to compare with Autotools
        'basename': filename,
        'installed_testdir': installed_testdir,
        'wrapper': '',
      },
      install_dir: installed_tests_metadir,
    )
  endif
endforeach

if get_option('installed_tests')
  install_data(
    'empty-configure',
    'make-test-app.sh',
    'make-test-bundles.sh',
    'make-test-runtime.sh',
    'test-configure',
    'testpython.py',

    install_dir: installed_testdir,
    install_mode: 'rwxr-xr-x',
  )
  install_data(
    '0001-Add-test-logo.patch',
    'data1',
    'data1.patch',
    'data2',
    'data2.patch',
    'hello.sh',
    'hello.tar.xz',
    'importme.py',
    'importme2.py',
    'libtest.sh',
    'module1.json',
    'module1.yaml',
    'module2.json',
    'module2.yaml',
    'org.test.Deprecated.MD5.archive.json',
    'org.test.Deprecated.MD5.archive.yaml',
    'org.test.Deprecated.MD5.file.json',
    'org.test.Deprecated.MD5.file.yaml',
    'org.test.Deprecated.SHA1.archive.json',
    'org.test.Deprecated.SHA1.archive.yaml',
    'org.test.Deprecated.SHA1.file.json',
    'org.test.Deprecated.SHA1.file.yaml',
    'org.test.Hello.png',
    'org.test.Hello.desktop',
    'org.test.Hello.appdata.xml',
    'Hello.desktop',
    'org.test.Hello.xml',
    'Hello.xml',
    'Hello.appdata.xml',
    'Hello-desktop.appdata.xml',
    'org.test.Python.json',
    'org.test.Python2.json',
    'session.conf.in',
    'source1.json',
    'source2.json',
    'test-rename.json',
    'test-rename-appdata.json',
    'test-runtime.json',
    'test.json',
    'test.yaml',
    'org.flatpak_builder.gui.desktop',
    'org.flatpak_builder.gui.json',
    'org.flatpak_builder.gui.metainfo.xml',
    'org.flatpak.appstream_media.json',
    'org.test.Hello-256.png',

    install_dir: installed_testdir,
    install_mode: 'rw-r--r--',
  )
endif

subdir('test-keyring')
subdir('test-keyring2')
