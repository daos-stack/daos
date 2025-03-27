
lib_include_dir = [include_directories('.')]

libvfio_user_cflags = []

libvfio_user_sources = [
    'dma.c',
    'irq.c',
    'libvfio-user.c',
    'migration.c',
    'pci.c',
    'pci_caps.c',
    'tran.c',
    'tran_sock.c',
]

if opt_tran_pipe
    libvfio_user_sources += ['tran_pipe.c']
    libvfio_user_cflags += ['-DWITH_TRAN_PIPE']
endif

libvfio_user_deps = [
    json_c_dep,
]

libvfio_user = library(
    'vfio-user',
    sources: libvfio_user_sources,
    c_args: libvfio_user_cflags + common_cflags,
    dependencies: libvfio_user_deps,
    include_directories: public_include_dir,
    gnu_symbol_visibility: 'hidden',
    # We're not providing a stable ABI yet, so
    # this remains 0 regardless of API changes
    soversion: 0,
    version: '0.0.1',
    install: true,
    install_rpath: rpathdir,
)

libvfio_user_dep = declare_dependency(
    link_with: libvfio_user,
    dependencies: libvfio_user_deps,
    include_directories: public_include_dir,
)

libvfio_so_dir = meson.current_build_dir()
