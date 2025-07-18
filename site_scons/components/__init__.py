# Copyright 2016-2024 Intel Corporation
# Copyright 2025 Google LLC
# Copyright 2025 Hewlett Packard Enterprise Development LP
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# -*- coding: utf-8 -*-
"""Defines common components used by HPDD projects"""

import os
import platform

import distro
from prereq_tools import CopyRetriever, GitRepoRetriever
from SCons.Script import Dir, GetOption

# Check if this is an ARM platform
PROCESSOR = platform.machine()
ARM_LIST = ["ARMv7", "armeabi", "aarch64", "arm64"]
ARM_PLATFORM = False
if PROCESSOR.lower() in [x.lower() for x in ARM_LIST]:
    ARM_PLATFORM = True


class InstalledComps():
    """Checks for installed components and keeps track of prior checks"""

    installed = []
    not_installed = []

    def __init__(self, reqs):
        """Class for checking installed components"""
        self.reqs = reqs

    def inst(self, name):
        """Return True if name is in list of possible installed packages"""
        return set([name, 'all']).intersection(set(self.reqs.installed))

    def check(self, name):
        """Returns True if installed"""
        if name in self.installed:
            return True
        if name in self.not_installed:
            return False
        if self.inst(name) and self.reqs.is_installed(name):
            print(f'Using installed version of {name}')
            self.installed.append(name)
            return True

        if not GetOption('help') and not GetOption('silent'):
            print(f'Using build version of {name}')
        self.not_installed.append(name)
        return False


def include(reqs, name, use_value, exclude_value):
    """Return True if in include list"""
    if reqs.included(name):
        print(f'Including {name} optional component from build')
        return use_value
    if not GetOption('help'):
        print(f'Excluding {name} optional component from build')
    return exclude_value


def inst(reqs, name):
    """Return True if name is in list of installed packages"""
    installed = InstalledComps(reqs)
    return installed.check(name)


def check(reqs, name, built_str, installed_str=""):
    """Return a different string based on whether a component is installed or not"""
    installed = InstalledComps(reqs)
    if installed.check(name):
        return installed_str
    return built_str


def ofi_config(config):
    """Check ofi version"""
    if not GetOption('silent'):
        print('Checking for libfabric > 1.11...', end=' ')
    code = """#include <rdma/fabric.h>
_Static_assert(FI_MAJOR_VERSION == 1 && FI_MINOR_VERSION >= 11,
               "libfabric must be >= 1.11");"""
    rc = config.TryCompile(code, ".c")
    if not GetOption('silent'):
        print('yes' if rc else 'no')
    return rc


def define_mercury(reqs):
    """Mercury definitions"""
    libs = ['rt']

    if reqs.get_env('PLATFORM') == 'darwin':
        libs = []
    else:
        reqs.define('rt', libs=['rt'])

    # pylint: disable-next=wrong-spelling-in-comment,fixme
    # TODO: change to --enable-opx once upgraded to libfabric 1.17+
    ofi_build = ['./configure',
                 '--prefix=$OFI_PREFIX',
                 '--libdir=$OFI_PREFIX/lib64',
                 '--with-dlopen',
                 '--disable-static',
                 '--disable-silent-rules',
                 '--enable-sockets',
                 '--enable-tcp',
                 '--enable-verbs',
                 '--enable-rxm',
                 '--enable-shm',
                 '--enable-psm2',
                 '--enable-opx',
                 '--disable-efa',
                 '--disable-dmabuf_peer_mem',
                 '--disable-hook_hmem',
                 '--disable-hook_debug',
                 '--disable-trace',
                 '--disable-perf',
                 '--disable-rxd',
                 '--disable-mrail',
                 '--disable-udp',
                 '--disable-psm3']

    if reqs.target_type == 'debug':
        ofi_build.append('--enable-debug')
    else:
        ofi_build.append('--disable-debug')

    reqs.define('ofi',
                retriever=CopyRetriever(),
                commands=[['./autogen.sh'],
                          ofi_build,
                          ['make'],
                          ['make', 'install']],
                libs=['fabric'],
                config_cb=ofi_config,
                headers=['rdma/fabric.h'],
                pkgconfig='libfabric',
                package='libfabric-devel' if inst(reqs, 'ofi') else None,
                patch_rpath=['lib64'],
                build_env={'CFLAGS': "-fstack-usage -fPIC"})

    ucx_configure = ['./configure', '--disable-assertions', '--disable-params-check', '--enable-mt',
                     '--without-go', '--without-java', '--prefix=$UCX_PREFIX',
                     '--libdir=$UCX_PREFIX/lib64', '--enable-cma', '--without-cuda',
                     '--without-gdrcopy', '--with-verbs', '--without-knem', '--without-rocm',
                     '--without-xpmem', '--without-fuse3', '--without-ugni']

    if reqs.target_type == 'debug':
        ucx_configure.extend(['--enable-debug'])
    else:
        ucx_configure.extend(['--disable-debug', '--disable-logging'])

    reqs.define('ucx',
                retriever=CopyRetriever(),
                libs=['ucs', 'ucp', 'uct'],
                functions={'ucs': ['ucs_debug_disable_signal']},
                headers=['uct/api/uct.h'],
                pkgconfig='ucx',
                commands=[['./autogen.sh'],
                          ucx_configure,
                          ['make'],
                          ['make', 'install'],
                          ['mkdir', '-p', '$UCX_PREFIX/lib64/pkgconfig'],
                          ['cp', 'ucx.pc', '$UCX_PREFIX/lib64/pkgconfig']],
                build_env={'CFLAGS': '-Wno-error'},
                package='ucx-devel' if inst(reqs, 'ucx') else None)

    mercury_build = ['cmake',
                     '-DBUILD_SHARED_LIBS:BOOL=ON',
                     '-DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo',
                     '-DCMAKE_CXX_FLAGS:STRING="-std=c++11"',
                     '-DCMAKE_INSTALL_PREFIX:PATH=$MERCURY_PREFIX',
                     '-DMERCURY_INSTALL_LIB_DIR:PATH=$MERCURY_PREFIX/lib64',
                     '-DMERCURY_INSTALL_DATA_DIR:PATH=$MERCURY_PREFIX/lib64',
                     '-DNA_INSTALL_PLUGIN_DIR:PATH=$MERCURY_PREFIX/lib64/mercury',
                     '-DBUILD_DOCUMENTATION:BOOL=OFF',
                     '-DBUILD_EXAMPLES:BOOL=OFF',
                     '-DBUILD_TESTING:BOOL=ON',
                     '-DBUILD_TESTING_PERF:BOOL=ON',
                     '-DBUILD_TESTING_UNIT:BOOL=OFF',
                     '-DMERCURY_USE_BOOST_PP:BOOL=ON',
                     '-DMERCURY_USE_SYSTEM_BOOST:BOOL=ON',
                     '-DMERCURY_USE_CHECKSUMS:BOOL=OFF',
                     '-DMERCURY_ENABLE_COUNTERS:BOOL=ON',
                     '-DNA_USE_DYNAMIC_PLUGINS:BOOL=ON',
                     '-DNA_USE_SM:BOOL=ON',
                     '-DNA_USE_OFI:BOOL=ON',
                     '-DNA_USE_UCX:BOOL=ON',
                     '../mercury']

    if reqs.target_type == 'debug':
        mercury_build.append('-DMERCURY_ENABLE_DEBUG:BOOL=ON')
    else:
        mercury_build.append('-DMERCURY_ENABLE_DEBUG:BOOL=OFF')

    reqs.define('mercury',
                retriever=CopyRetriever(),
                commands=[mercury_build,
                          ['make'],
                          ['make', 'install']],
                libs=['mercury'],
                pkgconfig='mercury',
                requires=['boost', 'ofi', 'ucx'] + libs,
                out_of_src_build=True,
                package='mercury-devel' if inst(reqs, 'mercury') else None,
                build_env={'CFLAGS': '-fstack-usage'})


def define_common(reqs):
    """Common system component definitions"""
    reqs.define('cmocka', libs=['cmocka'], package='libcmocka-devel')

    reqs.define('libunwind', libs=['unwind'], headers=['libunwind.h'], package='libunwind-devel')

    reqs.define('lz4', headers=['lz4.h'], package='lz4-devel')

    reqs.define('valgrind_devel', headers=['valgrind/valgrind.h'], package='valgrind-devel')

    reqs.define('cunit', libs=['cunit'], headers=['CUnit/Basic.h'], package='CUnit-devel')

    reqs.define('boost', headers=['boost/preprocessor.hpp'], package='boost-python36-devel')

    reqs.define('yaml', headers=['yaml.h'], package='libyaml-devel')

    reqs.define('event', libs=['event'], package='libevent-devel')

    reqs.define('crypto', libs=['crypto'], headers=['openssl/md5.h'], package='openssl-devel')

    reqs.define('json-c', libs=['json-c'], headers=['json-c/json.h'], package='json-c-devel')

    reqs.define('uuid', libs=['uuid'], headers=['uuid/uuid.h'], package='libuuid-devel')

    reqs.define('hwloc', libs=['hwloc'], headers=['hwloc.h'], package='hwloc-devel')

    if ARM_PLATFORM:
        reqs.define('ipmctl', skip_arch=True)
    else:
        reqs.define('ipmctl', headers=['nvm_management.h'], package='libipmctl-devel')


def define_ompi(reqs):
    """OMPI and related components"""
    reqs.define('ompi', pkgconfig='ompi', package='ompi-devel')
    reqs.define('mpich', pkgconfig='mpich', package='mpich-devel')


def define_components(reqs):
    """Define all of the components"""
    define_common(reqs)
    define_mercury(reqs)
    define_ompi(reqs)

    reqs.define('isal',
                retriever=CopyRetriever(),
                commands=[['./autogen.sh'],
                          ['./configure', '--prefix=$ISAL_PREFIX', '--libdir=$ISAL_PREFIX/lib64'],
                          ['make'],
                          ['make', 'install']],
                libs=['isal'])
    reqs.define('isal_crypto',
                retriever=CopyRetriever(),
                commands=[['./autogen.sh'],
                          ['./configure',
                           '--prefix=$ISAL_CRYPTO_PREFIX',
                           '--libdir=$ISAL_CRYPTO_PREFIX/lib64'],
                          ['make'],
                          ['make', 'install']],
                libs=['isal_crypto'])

    reqs.define('pmdk',
                retriever=CopyRetriever(),
                commands=[['make',
                           'all',
                           'BUILD_EXAMPLES=n',
                           'BUILD_BENCHMARKS=n',
                           'DOC=y',
                           'EXTRA_CFLAGS="-Wno-error"',
                           'install',
                           'prefix=$PMDK_PREFIX',
                           'libdir=$PMDK_PREFIX/lib64'],
                          ['mkdir', '-p', '$PMDK_PREFIX/share/pmdk'],
                          ['cp', 'utils/pmdk.magic', '$PMDK_PREFIX/share/pmdk'],
                          ['fdupes', '-s', '$PMDK_PREFIX/share/man']],
                libs=['pmemobj'])
    abt_build = ['./configure',
                 '--prefix=$ARGOBOTS_PREFIX',
                 '--libdir=$ARGOBOTS_PREFIX/lib64',
                 'CC=gcc',
                 '--enable-stack-unwind=yes']
    try:
        if reqs.get_env('SANITIZERS') != "":
            # NOTE the address sanitizer library add some extra info on the stack and thus ULTs
            # need a bigger stack
            print("Increase argobots default stack size from 16384 to 32768")
            abt_build += ['--enable-default-stacksize=32768']
    except KeyError:
        pass

    if reqs.target_type == 'debug':
        abt_build.append('--enable-debug=most')
    else:
        abt_build.append('--disable-debug')

    if inst(reqs, 'valgrind_devel'):
        abt_build.append('--enable-valgrind')

    reqs.define('argobots',
                retriever=CopyRetriever(),
                commands=[['./autogen.sh'],
                          abt_build,
                          ['make'],
                          ['make', 'install']],
                requires=['libunwind'],
                libs=['abt'],
                headers=['abt.h'])

    reqs.define('fuse', libs=['fuse3'], defines=['FUSE_USE_VERSION=35'],
                retriever=GitRepoRetriever(),
                commands=[['meson', 'setup', '--prefix=$FUSE_PREFIX', '-Ddisable-mtab=True',
                           '-Dudevrulesdir=$FUSE_PREFIX/udev', '-Dutils=False',
                           '--default-library', 'both', '../fuse'],
                          ['ninja', 'install']],
                headers=['fuse3/fuse.h'],
                required_progs=['libtoolize', 'ninja', 'meson'],
                out_of_src_build=True)

    reqs.define('fused', libs=['fused'], defines=['FUSE_USE_VERSION=35'],
                retriever=CopyRetriever(),
                commands=[['meson', 'setup', '--prefix=$FUSED_PREFIX', '-Ddisable-mtab=True',
                           '-Dudevrulesdir=$FUSED_PREFIX/udev', '-Dutils=False',
                           '--default-library', 'static', '../fused'],
                          ['meson', 'setup', '--reconfigure', '../fused'],
                          ['ninja', 'install']],
                pkgconfig='fused',
                headers=['fused/fuse.h'],
                required_progs=['libtoolize', 'ninja', 'meson'],
                out_of_src_build=True)

    # Tell SPDK which CPU to optimize for, by default this is native which works well unless you
    # are relocating binaries across systems, for example in CI under GitHub actions etc.  There
    # isn't a minimum value needed here, but getting this wrong will cause daos server to exit
    # prematurely with SIGILL (-4).
    # https://docs.microsoft.com/en-us/azure/virtual-machines/dv2-dsv2-series#dsv2-series says
    # that GHA can schedule on any of Skylake, Broadwell or Haswell CPUs.
    # Ubuntu systems seem to fail more often, there may be something different going on here,
    # it has also failed with sandybridge.
    # https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html
    dist = distro.linux_distribution()
    if ARM_PLATFORM:
        spdk_arch = 'native'
    elif dist[0] == 'CentOS Linux' and dist[1] == '7':
        spdk_arch = 'native'
    elif dist[0] == 'Ubuntu' and dist[1] == '20.04':
        spdk_arch = 'nehalem'
    else:
        spdk_arch = 'haswell'

    copy_files = os.path.join(Dir('#').abspath, 'utils/scripts/copy_files.sh')
    reqs.define('spdk',
                retriever=CopyRetriever(),
                commands=[['./configure',
                           '--prefix=$SPDK_PREFIX',
                           '--disable-tests',
                           '--disable-unit-tests',
                           '--disable-apps',
                           '--without-vhost',
                           '--without-crypto',
                           '--without-pmdk',
                           '--without-rbd',
                           '--without-iscsi-initiator',
                           '--without-isal',
                           '--without-vtune',
                           '--with-shared',
                           f'--target-arch={spdk_arch}'],
                          ['make', f'CONFIG_ARCH={spdk_arch}'],
                          ['make', 'libdir=$SPDK_PREFIX/lib64/daos_srv',
                           'includedir=$SPDK_PREFIX/include/daos_srv', 'install'],
                          [copy_files, 'dpdk/build/lib', '$SPDK_PREFIX/lib64/daos_srv'],
                          [copy_files, 'dpdk/build/include', '$SPDK_PREFIX/include/daos_srv/dpdk'],
                          [copy_files, 'include', '$SPDK_PREFIX/share/daos/spdk/include'],
                          [copy_files, 'scripts', '$SPDK_PREFIX/share/daos/spdk/scripts'],
                          ['mv', '$SPDK_PREFIX/bin/spdk_nvme_discovery_aer',
                           '$SPDK_PREFIX/bin/daos_spdk_nvme_discovery_aer'],
                          ['cp', 'build/examples/lsvmd', '$SPDK_PREFIX/bin/daos_spdk_nvme_lsvmd'],
                          ['cp', 'build/examples/nvme_manage',
                           '$SPDK_PREFIX/bin/daos_spdk_nvme_manage'],
                          ['mv', '$SPDK_PREFIX/bin/spdk_nvme_identify',
                           '$SPDK_PREFIX/bin/daos_spdk_nvme_identify'],
                          ['cp', '$SPDK_PREFIX/bin/spdk_nvme_perf',
                           '$SPDK_PREFIX/bin/daos_spdk_nvme_perf']],
                headers=['spdk/nvme.h'],
                extra_lib_path=['lib64/daos_srv'],
                extra_include_path=['include/daos_srv'],
                patch_rpath=['lib64/daos_srv', 'bin'])

    reqs.define('protobufc',
                retriever=CopyRetriever(),
                commands=[['./autogen.sh'],
                          ['./configure', '--prefix=$PROTOBUFC_PREFIX', '--disable-protoc'],
                          ['make'],
                          ['make', 'install']],
                libs=['protobuf-c'],
                headers=['protobuf-c/protobuf-c.h'],
                package='protobuf-c-devel')

    os_name = dist[0].split()[0]
    if os_name == 'Ubuntu':
        capstone_pkg = 'libcapstone-dev'
        libaio_pkg = 'libaio-dev'
    elif os_name == 'openSUSE':
        capstone_pkg = 'libcapstone-devel'
        libaio_pkg = 'libaio-devel'
    else:
        capstone_pkg = 'capstone-devel'
        libaio_pkg = 'libaio-devel'
    reqs.define('capstone', libs=['capstone'], headers=['capstone/capstone.h'],
                package=capstone_pkg)
    reqs.define('aio', libs=['aio'], headers=['libaio.h'], package=libaio_pkg)


__all__ = ['define_components']
