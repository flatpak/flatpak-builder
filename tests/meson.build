test_names = [
  'test-builder',
  'test-builder-python'
]
message(meson.current_build_dir() / '../src')
message(run_command(find_program('sh'), '-c', 'echo $PATH').stdout())
foreach name : test_names

  # FIXME: the "test-builder" test makes flatpak-builder core dump. No idea if it'll core dump in CI too… And the other test doesn't work as it requires python2 (!) which is no longer in F33?
  test(name, find_program(name + '.sh'),
    # protocol: 'tap',
    env: [
      'FLATPAK_TESTS_DEBUG=1',
      # FIXME: the different tests shell scripts want to run the built flatpak-builder, so its path must be added to the PATH, path being $build_root/src. Nevertheless, this is a very big hack so I wonder if a better solution that looks less autotools-like exists for that?
      'PATH=@0@:@1@'.format(
        meson.current_build_dir() / '../src',
        run_command(find_program('sh'), '-c', 'echo $PATH').stdout()
      ),
    ],
    # FIXME: see above
    # depends: flatpak_builder_prog,
    workdir: meson.current_build_dir(),
  )
endforeach
