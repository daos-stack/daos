#!/usr/bin/python
# Copyright 2016-2022 Intel Corporation
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

import sys
import platform
import distro
from prereq_tools import GitRepoRetriever
# from prereq_tools import WebRetriever

SCONS_EXE = sys.argv[0]
# Check if this is an ARM platform
PROCESSOR = platform.machine()
ARM_LIST = ["ARMv7", "armeabi", "aarch64", "arm64"]
ARM_PLATFORM = False
if PROCESSOR.lower() in [x.lower() for x in ARM_LIST]:
    ARM_PLATFORM = True


class installed_comps():
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
            print("Using installed version of %s" % name)
            self.installed.append(name)
            return True

        print("Using build version of %s" % name)
        self.not_installed.append(name)
        return False


def include(reqs, name, use_value, exclude_value):
    """Return True if in include list"""
    if set([name, 'all']).intersection(set(reqs.include)):
        print("Including %s optional component from build" % name)
        return use_value
    print("Excluding %s optional component from build" % name)
    return exclude_value


def inst(reqs, name):
    """Return True if name is in list of installed packages"""
    installed = installed_comps(reqs)
    return installed.check(name)


def check(reqs, name, built_str, installed_str=""):
    """Return a different string based on whether a component is
       installed or not"""
    installed = installed_comps(reqs)
    if installed.check(name):
        return installed_str
    return built_str


def ofi_config(config):
    """Check ofi version"""
    code = """#include <rdma/fabric.h>
_Static_assert(FI_MAJOR_VERSION == 1 && FI_MINOR_VERSION >= 11,
               "libfabric must be >= 1.11");"""
    return config.TryCompile(code, ".c")


def define_mercury(reqs):
    """mercury definitions"""
    libs = ['rt']

    if reqs.get_env('PLATFORM') == 'darwin':
        libs = []
    else:
        reqs.define('rt', libs=['rt'])
    reqs.define('stdatomic', headers=['stdatomic.h'])

    if reqs.check_component('stdatomic'):
        atomic = 'stdatomic'
    else:
        atomic = 'openpa'

    reqs.define('psm2',
                retriever=GitRepoRetriever('https://github.com/intel/opa-psm2.git'),
                # psm2 hard-codes installing into /usr/...
                commands=[['sed',
                           '-i',
                           '-e',
                           's/\\(.{DESTDIR}\\/\\)usr\\//\\1/',
                           '-e',
                           's/\\(INSTALL_LIB_TARG=\\/usr\\/lib\\)64/\\1/',
                           '-e',
                           's/\\(INSTALL_LIB_TARG=\\)\\/usr/\\1/',
                           'Makefile',
                           'compat/Makefile'],
                          ['make', 'LIBDIR=/lib64'],
                          ['make', 'DESTDIR=$PSM2_PREFIX', 'LIBDIR=/lib64', 'install']],
                headers=['psm2.h'],
                libs=['psm2'])

    ofi_build = ['./configure',
                 '--prefix=$OFI_PREFIX',
                 '--disable-efa',
                 '--disable-psm3',
                 '--without-gdrcopy']
    if reqs.target_type == 'debug':
        ofi_build.append('--enable-debug')
    else:
        ofi_build.append('--disable-debug')

    ofi_build.extend(include(reqs,
                             'psm2',
                             check(reqs,
                                   'psm2',
                                   ['--enable-psm2=$PSM2_PREFIX',
                                    'LDFLAGS=-Wl,--enable-new-dtags -Wl,-rpath=$PSM2_PREFIX/lib64'],
                                   ['--enable-psm2']),
                             ['--disable-psm2']))

    reqs.define('ofi',
                retriever=GitRepoRetriever('https://github.com/ofiwg/libfabric'),
                commands=[['./autogen.sh'],
                          ofi_build,
                          ['make'],
                          ['make', 'install']],
                libs=['fabric'],
                requires=include(reqs, 'psm2', ['psm2'], []),
                config_cb=ofi_config,
                headers=['rdma/fabric.h'],
                package='libfabric-devel' if inst(reqs, 'ofi') else None,
                patch_rpath=['lib'])

    reqs.define('openpa',
                retriever=GitRepoRetriever('https://github.com/pmodels/openpa.git'),
                commands=[['libtoolize'],
                          ['./autogen.sh'],
                          ['./configure', '--prefix=$OPENPA_PREFIX'],
                          ['make'],
                          ['make', 'install']],
                libs=['opa'],
                package='openpa-devel' if inst(reqs, 'openpa') else None)

    reqs.define('ucx', libs=['ucp'])

    mercury_build = ['cmake',
                     '-DMERCURY_USE_CHECKSUMS=OFF',
                     '-DOPA_INCLUDE_DIR=$OPENPA_PREFIX/include/',
                     '-DCMAKE_INSTALL_PREFIX=$MERCURY_PREFIX',
                     '-DCMAKE_CXX_FLAGS="-std=c++11"',
                     '-DBUILD_EXAMPLES=OFF',
                     '-DMERCURY_USE_BOOST_PP=ON',
                     '-DBUILD_TESTING=OFF',
                     '-DNA_USE_OFI=ON',
                     '-DBUILD_DOCUMENTATION=OFF',
                     '-DBUILD_SHARED_LIBS=ON',
                     '../mercury']

    if reqs.target_type == 'debug':
        mercury_build.append('-DMERCURY_ENABLE_DEBUG=ON')
    else:
        mercury_build.append('-DMERCURY_ENABLE_DEBUG=OFF')

    if reqs.get_env('UCX'):
        mercury_build.extend(['-DNA_USE_UCX=ON',
                              '-DUCX_INCLUDE_DIR=/usr/include',
                              '-DUCP_LIBRARY=/usr/lib64/libucp.so',
                              '-DUCS_LIBRARY=/usr/lib64/libucs.so',
                              '-DUCT_LIBRARY=/usr/lib64/libuct.so'])
        libs.append('ucx')

    mercury_build.append(check(reqs,
                               'openpa',
                               '-DOPA_LIBRARY=$OPENPA_PREFIX/lib/libopa.a',
                               '-DOPA_LIBRARY=$OPENPA_PREFIX/lib64/libopa.a'))

    mercury_build.extend(check(reqs,
                               'ofi',
                               ['-DOFI_INCLUDE_DIR=$OFI_PREFIX/include',
                                '-DOFI_LIBRARY=$OFI_PREFIX/lib/libfabric.so'],
                               []))

    reqs.define('mercury',
                retriever=GitRepoRetriever('https://github.com/mercury-hpc/mercury.git', True),
                commands=[mercury_build,
                          ['make'],
                          ['make', 'install']],
                libs=['mercury', 'na', 'mercury_util'],
                pkgconfig='mercury',
                requires=[atomic, 'boost', 'ofi'] + libs,
                out_of_src_build=True,
                package='mercury-devel' if inst(reqs, 'mercury') else None,
                patch_rpath=['lib'])


def define_common(reqs):
    """common system component definitions"""
    reqs.define('cmocka', libs=['cmocka'], package='libcmocka-devel')

    reqs.define('libunwind', libs=['unwind'], headers=['libunwind.h'],
                package='libunwind-devel')

    reqs.define('lz4', headers=['lz4.h'], package='lz4-devel')

    reqs.define('valgrind_devel', headers=['valgrind/valgrind.h'],
                package='valgrind-devel')

    reqs.define('cunit', libs=['cunit'], headers=['CUnit/Basic.h'],
                package='CUnit-devel')

    reqs.define('python34_devel', headers=['python3.4m/Python.h'],
                package='python34-devel')

    reqs.define('libelf', headers=['libelf.h'], package='elfutils-libelf-devel')

    reqs.define('tbbmalloc', libs=['tbbmalloc_proxy'], package='tbb-devel')

    reqs.define('jemalloc', libs=['jemalloc'], package='jemalloc-devel')

    reqs.define('boost', headers=['boost/preprocessor.hpp'],
                package='boost-python36-devel')

    reqs.define('yaml', headers=['yaml.h'], package='libyaml-devel')

    reqs.define('event', libs=['event'], package='libevent-devel')

    reqs.define('crypto', libs=['crypto'], headers=['openssl/md5.h'],
                package='openssl-devel')

    reqs.define('json-c', libs=['json-c'], headers=['json-c/json.h'],
                package='json-c-devel')

    if reqs.get_env('PLATFORM') == 'darwin':
        reqs.define('uuid', headers=['uuid/uuid.h'])
    else:
        reqs.define('uuid', libs=['uuid'], headers=['uuid/uuid.h'],
                    package='libuuid-devel')


def define_ompi(reqs):
    """OMPI and related components"""
    reqs.define('hwloc', headers=['hwloc.h'], libs=['hwloc'],
                package='hwloc-devel')
    reqs.define('ompi', pkgconfig='ompi', package='ompi-devel')
    reqs.define('mpich', pkgconfig='mpich', package='mpich-devel')


def define_components(reqs):
    """Define all of the components"""
    define_common(reqs)
    define_mercury(reqs)
    define_ompi(reqs)

    reqs.define('isal',
                retriever=GitRepoRetriever('https://github.com/01org/isa-l.git'),
                commands=[['./autogen.sh'],
                          ['./configure', '--prefix=$ISAL_PREFIX', '--libdir=$ISAL_PREFIX/lib'],
                          ['make'],
                          ['make', 'install']],
                libs=['isal'])
    reqs.define('isal_crypto',
                retriever=GitRepoRetriever('https://github.com/intel/isa-l_crypto'),
                commands=[['./autogen.sh'],
                          ['./configure',
                           '--prefix=$ISAL_CRYPTO_PREFIX',
                           '--libdir=$ISAL_CRYPTO_PREFIX/lib'],
                          ['make'],
                          ['make', 'install']],
                libs=['isal_crypto'])

    reqs.define('pmdk',
                retriever=GitRepoRetriever('https://github.com/pmem/pmdk.git'),
                commands=[['make',
                           'all',
                           'BUILD_RPMEM=n',
                           'NDCTL_ENABLE=n',
                           'NDCTL_DISABLE=y',
                           'DOC=n',
                           'install',
                           'prefix=$PMDK_PREFIX']],
                libs=['pmemobj'])
    abt_build = ['./configure',
                 '--prefix=$ARGOBOTS_PREFIX',
                 'CC=gcc',
                 '--enable-stack-unwind']

    if reqs.target_type == 'debug':
        abt_build.append('--enable-debug=most')
    else:
        abt_build.append('--disable-debug')

    if inst(reqs, 'valgrind_devel'):
        abt_build.append('--enable-valgrind')

    reqs.define('argobots',
                retriever=GitRepoRetriever('https://github.com/pmodels/argobots.git', True),
                commands=[['git', 'clean', '-dxf'],
                          ['./autogen.sh'],
                          abt_build,
                          ['make'],
                          ['make', 'install']],
                requires=['libunwind'],
                libs=['abt'],
                headers=['abt.h'])

    reqs.define('fuse', libs=['fuse3'], defines=['FUSE_USE_VERSION=35'],
                headers=['fuse3/fuse.h'], package='fuse3-devel')

    # Tell SPDK which CPU to optimize for, by default this is native which works well unless you
    # are relocating binaries across systems, for example in CI under github actions etc.  There
    # isn't a minimum value needed here, but getting this wrong will cause daos server to exit
    # prematurely with SIGILL (-4).
    # https://docs.microsoft.com/en-us/azure/virtual-machines/dv2-dsv2-series#dsv2-series says
    # that GHA can schedule on any of Skylake, Broadwell or Haswell CPUs.
    # Ubuntu systems seem to fail more often, there may be something different going on here,
    # it has also failed with sandybridge.
    # https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html
    dist = distro.linux_distribution()
    if dist[0] == 'CentOS Linux' and dist[1] == '7':
        spdk_arch = 'native'
    elif dist[0] == 'Ubuntu' and dist[1] == '20.04':
        spdk_arch = 'nehalem'
    else:
        spdk_arch = 'haswell'

    reqs.define('spdk',
                retriever=GitRepoRetriever('https://github.com/spdk/spdk.git', True),
                commands=[['./configure',
                           '--prefix=$SPDK_PREFIX',
                           '--disable-tests',
                           '--disable-unit-tests',
                           '--disable-apps',
                           '--without-vhost',
                           '--without-crypto',
                           '--without-pmdk',
                           '--without-rbd',
                           '--with-rdma',
                           '--without-iscsi-initiator',
                           '--without-isal',
                           '--without-vtune',
                           '--with-shared'],
                          ['make', 'CONFIG_ARCH={}'.format(spdk_arch)],
                          ['make', 'install'],
                          ['cp', '-r', '-P', 'dpdk/build/lib/', '$SPDK_PREFIX'],
                          ['cp', '-r', '-P', 'dpdk/build/include/', '$SPDK_PREFIX/include/dpdk'],
                          ['mkdir', '-p', '$SPDK_PREFIX/share/spdk'],
                          ['cp', '-r', 'include', 'scripts', '$SPDK_PREFIX/share/spdk'],
                          ['cp', 'build/examples/lsvmd', '$SPDK_PREFIX/bin/spdk_nvme_lsvmd'],
                          ['cp', 'build/examples/nvme_manage', '$SPDK_PREFIX/bin/spdk_nvme_manage'],
                          ['cp', 'build/examples/identify', '$SPDK_PREFIX/bin/spdk_nvme_identify'],
                          ['cp', 'build/examples/perf', '$SPDK_PREFIX/bin/spdk_nvme_perf']],
                headers=['spdk/nvme.h', 'dpdk/rte_eal.h'],
                extra_include_path=['/usr/include/dpdk',
                                    '$SPDK_PREFIX/include/dpdk',
                                    # debian dpdk rpm puts rte_config.h here
                                    '/usr/include/x86_64-linux-gnu/dpdk'],
                patch_rpath=['lib'])

    reqs.define('protobufc',
                retriever=GitRepoRetriever('https://github.com/protobuf-c/protobuf-c.git'),
                commands=[['./autogen.sh'],
                          ['./configure', '--prefix=$PROTOBUFC_PREFIX', '--disable-protoc'],
                          ['make'],
                          ['make', 'install']],
                libs=['protobuf-c'],
                headers=['protobuf-c/protobuf-c.h'])


__all__ = ['define_components']
