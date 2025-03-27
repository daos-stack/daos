
client_sources = [
    'client.c',
    '../lib/migration.c',
    '../lib/tran.c',
    '../lib/tran_sock.c',
]

client_deps = [
    json_c_dep,
    thread_dep,
]

client = executable(
    'client',
    client_sources,
    c_args: common_cflags,
    dependencies: client_deps,
    include_directories: public_include_dir + lib_include_dir,
    install: false,
)


server_sources = [
    'server.c',
]

server_deps = [
    libvfio_user_dep,
]

server = executable(
    'server',
    server_sources,
    c_args: common_cflags,
    dependencies: server_deps,
    include_directories: lib_include_dir,
    install: false,
)


null_sources = [
    'null.c',
]

null_deps = [
    libvfio_user_dep,
    thread_dep,
]

null = executable(
    'null',
    null_sources,
    c_args: common_cflags,
    dependencies: null_deps,
    include_directories: lib_include_dir,
    install: false,
)


gpio_pci_idio_16_sources = [
    'gpio-pci-idio-16.c',
]

gpio_pci_idio_16_deps = [
    libvfio_user_dep,
]

gpio_pci_idio_16 = executable(
    'gpio-pci-idio-16',
    gpio_pci_idio_16_sources,
    c_args: common_cflags,
    dependencies: gpio_pci_idio_16_deps,
    include_directories: lib_include_dir,
    install: false,
)


lspci_sources = [
    'lspci.c',
]

lspci_deps = [
    libvfio_user_dep,
]

lspci = executable(
    'lspci',
    lspci_sources,
    c_args: common_cflags,
    dependencies: lspci_deps,
    include_directories: lib_include_dir,
    install: false,
)
