
if valgrind.found()
    valgrind_common_wrapper = [
        valgrind,
        '--quiet',
        '--track-origins=yes',
        '--error-exitcode=1',
        '--exit-on-first-error=yes',
        '--leak-check=full',
    ]

    valgrind_unit_wrapper = valgrind_common_wrapper + [
        '--suppressions=' + ( meson.current_source_dir() / 'valgrind.supp' ),
        '--show-leak-kinds=all',
    ]
    if meson.version().version_compare('<0.57.0')
        add_test_setup('valgrind',
                       exe_wrapper: valgrind_unit_wrapper)
    else
        add_test_setup('valgrind',
                       exclude_suites: ['functional', 'pyunit', 'style'],
                       exe_wrapper: valgrind_unit_wrapper)
    endif
endif

unit_tests_sources = [
    'unit-tests.c',
    'mocks.c',
    '../lib/dma.c',
    '../lib/irq.c',
    '../lib/libvfio-user.c',
    '../lib/migration.c',
    '../lib/pci.c',
    '../lib/pci_caps.c',
    '../lib/tran.c',
    '../lib/tran_pipe.c',
    '../lib/tran_sock.c',
]

unit_tests_deps = [
    json_c_dep,
    cmocka_dep,
    dl_dep,
]
unit_tests_cflags = [
    '-DUNIT_TEST',
    '-DWITH_TRAN_PIPE',
]

unit_tests = executable(
    'unit_tests',
    unit_tests_sources,
    c_args: unit_tests_cflags + common_cflags,
    dependencies: unit_tests_deps,
    include_directories: public_include_dir + lib_include_dir,
    install: false,
)

test(
    'unit_tests',
    unit_tests,
    suite: 'unit',
)

test(
    'test-lspci',
    find_program('test-lspci.sh'),
    suite: 'functional',
    args: [lspci],
)

csenv = []
if opt_sanitizers != 'none'
    csenv += ['WITH_ASAN=1']
endif

test(
    'test-client-server',
    find_program('test-client-server.sh'),
    env: csenv,
    suite: 'functional',
    args: [client, server],
    timeout: 90,
)

if opt_sanitizers == 'none' and meson.version().version_compare('>=0.56.0')
    test(
        'test-linkage.sh',
        find_program('test-linkage.sh'),
        suite: 'functional',
        args: [
            meson.project_source_root(),
            meson.project_build_root(),
            ' '.join(cc.cmd_array()),
        ]
    )
endif

subdir('py')
