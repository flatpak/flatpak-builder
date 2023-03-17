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

foreach test_name : test_names
  test_script = find_program(test_name + '.sh')

  test(
    test_name,
    test_script,
    env: test_env,
    depends: flatpak_builder,
    workdir: meson.current_build_dir(),
  )
endforeach
