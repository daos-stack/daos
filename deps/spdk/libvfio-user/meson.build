project(
    'libvfio-user',
    'c',
    version: '0.0.1',
    license: 'BSD-3-Clause',
    meson_version: '>= 0.53.0',
    default_options: [
        'buildtype=debug',
        'c_std=gnu99',
        'warning_level=2',
        # clang with dwarf-5 can break valgrind
        'c_args=-gdwarf-4',
    ],
)

opt_rpath = get_option('rpath')
opt_tran_pipe = get_option('tran-pipe')
opt_debug_logs = get_option('debug-logs')
opt_sanitizers = get_option('b_sanitize')
opt_debug = get_option('debug')

cc = meson.get_compiler('c')

prefix = get_option('prefix')
libdir = prefix / get_option('libdir')

if prefix == '/usr' and not opt_rpath.enabled()
    rpathdir = ''
else
    rpathdir = libdir
endif

thread_dep = dependency('threads')
dl_dep = cc.find_library('dl', required: true)

json_c_version = '0.11'
json_c_dep = dependency('json-c', version: '>=' + json_c_version)

cmocka_version = ''
cmocka_dep = dependency('cmocka', version: '>=' + cmocka_version)


pytest = find_program('pytest-3', required: false)
flake8 = find_program('flake8', required: false)
rstlint = find_program('restructuredtext-lint', required: false)
valgrind = find_program('valgrind', required: false)

common_cflags = [
    '-D_GNU_SOURCE',
]

if opt_debug
    common_cflags += '-Werror'
endif

if opt_debug_logs.enabled() or (not opt_debug_logs.disabled() and opt_debug)
    common_cflags += ['-DDEBUG']
endif

if get_option('warning_level') == '2'
    # -Wall is set for 'warning_level>=1'
    # -Wextra is set for 'warning_level>=2'
    common_cflags += cc.get_supported_arguments([
        '-Wno-missing-field-initializers',
        '-Wmissing-declarations',
        '-Wwrite-strings',
    ])
endif

subdir('include')
subdir('lib')
subdir('samples')
subdir('test')
subdir('docs')
