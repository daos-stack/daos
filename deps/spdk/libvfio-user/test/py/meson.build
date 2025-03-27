
if valgrind.found()
    valgrind_pyunit_wrapper = valgrind_common_wrapper + [
        '--suppressions=' + ( meson.current_source_dir() / 'valgrind.supp' ),
        '--show-leak-kinds=definite',
        '--errors-for-leak-kinds=definite',
    ]
    valgrind_pyunit_env = ['PYTHONMALLOC=malloc']

    if meson.version().version_compare('<0.57.0')
        add_test_setup('pyvalgrind',
                       env: valgrind_pyunit_env,
                       exe_wrapper: valgrind_pyunit_wrapper,
                       timeout_multiplier: 10)
    else
        add_test_setup('pyvalgrind',
                       exclude_suites: ['functional', 'unit', 'style'],
                       env: valgrind_pyunit_env,
                       exe_wrapper: valgrind_pyunit_wrapper,
                       timeout_multiplier: 10)
    endif
endif

python_tests_common = [
    'libvfio_user.py',
]

python_tests = [
    'test_destroy.py',
    'test_device_get_info.py',
    'test_device_get_irq_info.py',
    'test_device_get_region_info.py',
    'test_device_get_region_info_zero_size.py',
    'test_device_get_region_io_fds.py',
    'test_device_set_irqs.py',
    'test_dirty_pages.py',
    'test_dma_map.py',
    'test_dma_unmap.py',
    'test_irq_trigger.py',
    'test_map_unmap_sg.py',
    'test_migration.py',
    'test_negotiate.py',
    'test_pci_caps.py',
    'test_pci_ext_caps.py',
    'test_quiesce.py',
    'test_request_errors.py',
    'test_setup_region.py',
    'test_vfu_create_ctx.py',
    'test_vfu_realize_ctx.py',
]

python_files = python_tests_common + python_tests

if pytest.found() and opt_sanitizers == 'none'
    foreach testname: python_tests
        test(testname,
             pytest,
             suite: 'pyunit',
             env: environment({'LIBVFIO_SO_DIR': libvfio_so_dir}),
             args: [files(testname)])
    endforeach
endif

if flake8.found()
    flake8_ignore = 'F405,F403,E128,E131,E127'
    test('flake8',
         flake8,
         suite: 'style',
         args: ['--extend-ignore', flake8_ignore,
                files(python_files)])
endif
