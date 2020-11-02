#!/usr/bin/python
# Copyright (c) 2016-2020 Intel Corporation
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
import os
import platform
from prereq_tools import GitRepoRetriever
from prereq_tools import WebRetriever
from prereq_tools import ProgramBinary

SCONS_EXE = sys.argv[0]
# Check if this is an ARM platform
PROCESSOR = platform.machine()
ARM_LIST = ["ARMv7", "armeabi", "aarch64", "arm64"]
ARM_PLATFORM = False
if PROCESSOR.lower() in [x.lower() for x in ARM_LIST]:
    ARM_PLATFORM = True

NINJA_PROG = ProgramBinary('ninja', ["ninja-build", "ninja"])

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

def exclude(reqs, name, use_value, exclude_value):
    """Return True if in exclude list"""
    if set([name, 'all']).intersection(set(reqs.exclude)):
        print("Excluding %s from build" % name)
        return exclude_value
    return use_value

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
                retriever=GitRepoRetriever(
                    'https://github.com/intel/opa-psm2.git'),
                # psm2 hard-codes installing into /usr/...
                commands=['sed -i -e "s/\\(.{DESTDIR}\\/\\)usr\\//\\1/" ' +
                          '       -e "s/\\(INSTALL_LIB_TARG=' +
                          '\\/usr\\/lib\\)64/\\1/" ' +
                          '       -e "s/\\(INSTALL_LIB_TARG=\\)\\/usr/\\1/" ' +
                          'Makefile compat/Makefile',
                          'make $JOBS_OPT LIBDIR="/lib64"',
                          'make DESTDIR=$PSM2_PREFIX LIBDIR="/lib64" install'],
                headers=['psm2.h'],
                libs=['psm2'])

    if reqs.build_type == 'debug':
        OFI_DEBUG = '--enable-debug '
    else:
        OFI_DEBUG = '--disable-debug '
    retriever = GitRepoRetriever('https://github.com/ofiwg/libfabric')
    reqs.define('ofi',
                retriever=retriever,
                commands=['./autogen.sh',
                          './configure --prefix=$OFI_PREFIX ' +
                          '--disable-efa ' +
                          OFI_DEBUG +
                          exclude(reqs, 'psm2',
                                  '--enable-psm2' +
                                  check(reqs, 'psm2',
                                        "=$PSM2_PREFIX "
                                        'LDFLAGS="-Wl,--enable-new-dtags ' +
                                        '-Wl,-rpath=$PSM2_PREFIX/lib64" ',
                                        ''),
                                  ''),
                          'make $JOBS_OPT',
                          'make install'],
                libs=['fabric'],
                requires=exclude(reqs, 'psm2', ['psm2'], []),
                config_cb=ofi_config,
                headers=['rdma/fabric.h'],
                package='libfabric-devel' if inst(reqs, 'ofi') else None,
                patch_rpath=['lib'])

    reqs.define('openpa',
                retriever=GitRepoRetriever(
                    'https://github.com/pmodels/openpa.git'),
                commands=['$LIBTOOLIZE', './autogen.sh',
                          './configure --prefix=$OPENPA_PREFIX',
                          'make $JOBS_OPT',
                          'make install'], libs=['opa'],
                package='openpa-devel' if inst(reqs, 'openpa') else None)

    if reqs.build_type == 'debug':
        MERCURY_DEBUG = '-DMERCURY_ENABLE_DEBUG=ON '
    else:
        MERCURY_DEBUG = '-DMERCURY_ENABLE_DEBUG=OFF '
    retriever = \
        GitRepoRetriever('https://github.com/mercury-hpc/mercury.git',
                         True)
    reqs.define('mercury',
                retriever=retriever,
                commands=['cmake -DMERCURY_USE_CHECKSUMS=OFF '
                          '-DOPA_LIBRARY=$OPENPA_PREFIX/lib' +
                          check(reqs, 'openpa', '', '64') + '/libopa.a '
                          '-DOPA_INCLUDE_DIR=$OPENPA_PREFIX/include/ '
                          '-DCMAKE_INSTALL_PREFIX=$MERCURY_PREFIX '
                          '-DBUILD_EXAMPLES=OFF '
                          '-DMERCURY_USE_BOOST_PP=ON '
                          '-DMERCURY_USE_SELF_FORWARD=ON '
                          '-DMERCURY_ENABLE_VERBOSE_ERROR=ON '
                          + MERCURY_DEBUG +
                          '-DBUILD_TESTING=ON '
                          '-DNA_USE_OFI=ON '
                          '-DBUILD_DOCUMENTATION=OFF '
                          '-DBUILD_SHARED_LIBS=ON ../mercury '
                          '-DMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE ' +
                          check(reqs, 'ofi',
                                '-DOFI_INCLUDE_DIR=$OFI_PREFIX/include '
                                '-DOFI_LIBRARY=$OFI_PREFIX/lib/libfabric.so'),
                          'make $JOBS_OPT', 'make install'],
                libs=['mercury', 'na', 'mercury_util'],
                requires=[atomic, 'boost', 'ofi'] + libs,
                extra_include_path=[os.path.join('include', 'na')],
                out_of_src_build=True,
                package='mercury-devel' if inst(reqs, 'mercury') else None,
                patch_rpath=['lib'])



def define_common(reqs):
    """common system component definitions"""
    reqs.define('lz4', headers=['lz4.h'], package='lz4-devel')

    reqs.define('valgrind_devel', headers=['valgrind/valgrind.h'],
                package='valgrind-devel')

    reqs.define('cunit', libs=['cunit'], headers=['CUnit/Basic.h'],
                package='CUnit-devel')

    reqs.define('python34_devel', headers=['python3.4m/Python.h'],
                package='python34-devel')

    reqs.define('python27_devel', headers=['python2.7/Python.h'],
                package='python-devel')

    reqs.define('libelf', headers=['libelf.h'], package='elfutils-libelf-devel')

    reqs.define('tbbmalloc', libs=['tbbmalloc_proxy'], package='tbb-devel')

    reqs.define('jemalloc', libs=['jemalloc'], package='jemalloc-devel')

    reqs.define('boost', headers=['boost/preprocessor.hpp'],
                package='boost-devel')

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

    isal_build = ['./autogen.sh ',
                  './configure --prefix=$ISAL_PREFIX --libdir=$ISAL_PREFIX/lib',
                  'make $JOBS_OPT', 'make install']
    reqs.define('isal',
                retriever=GitRepoRetriever(
                    'https://github.com/01org/isa-l.git'),
                commands=isal_build,
                libs=["isal"])
    reqs.define('isal_crypto',
                retriever=GitRepoRetriever("https://github.com/intel/"
                                           "isa-l_crypto"),
                commands=['./autogen.sh ',
                          './configure --prefix=$ISAL_CRYPTO_PREFIX '
                          '--libdir=$ISAL_CRYPTO_PREFIX/lib',
                          'make $JOBS_OPT', 'make install'],
                libs=['isal_crypto'])


    retriever = GitRepoRetriever("https://github.com/pmem/pmdk.git")

    pmdk_build = ["make all \"BUILD_RPMEM=n\" \"NDCTL_ENABLE=n\" "
                  "\"NDCTL_DISABLE=y\" $JOBS_OPT install "
                  "prefix=$PMDK_PREFIX"]

    reqs.define('pmdk',
                retriever=retriever,
                commands=pmdk_build,
                libs=["pmemobj"])

    retriever = GitRepoRetriever("https://github.com/pmodels/argobots.git",
                                 True)
    reqs.define('argobots',
                retriever=retriever,
                commands=['git clean -dxf ',
                          './autogen.sh',
                          './configure --prefix=$ARGOBOTS_PREFIX CC=gcc'
                          ' --enable-valgrind',
                          'make $JOBS_OPT',
                          'make $JOBS_OPT install'],
                requires=['valgrind_devel'],
                libs=['abt'],
                headers=['abt.h'])

    reqs.define('fuse', libs=['fuse3'], defines=["FUSE_USE_VERSION=35"],
                headers=['fuse3/fuse.h'], package='fuse3-devel')

    retriever = GitRepoRetriever("https://github.com/spdk/spdk.git", True)
    reqs.define('spdk',
                retriever=retriever,
                commands=['./configure --prefix="$SPDK_PREFIX"' \
                          ' --disable-tests --without-vhost --without-crypto' \
                          ' --without-pmdk --without-vpp --without-rbd' \
                          ' --with-rdma --with-shared' \
                          ' --without-iscsi-initiator --without-isal' \
                          ' --without-vtune', 'make $JOBS_OPT', 'make install',
                          'cp dpdk/build/lib/* "$SPDK_PREFIX/lib"',
                          'mkdir -p "$SPDK_PREFIX/share/spdk"',
                          'cp -r include scripts "$SPDK_PREFIX/share/spdk"'],
                libs=['rte_bus_pci'], patch_rpath=['lib'])

    url = 'https://github.com/protobuf-c/protobuf-c/releases/download/' \
        'v1.3.0/protobuf-c-1.3.0.tar.gz'
    web_retriever = WebRetriever(url, "08804f8bdbb3d6d44c2ec9e71e47ef6f")
    reqs.define('protobufc',
                retriever=web_retriever,
                commands=['./configure --prefix=$PROTOBUFC_PREFIX '
                          '--disable-protoc', 'make $JOBS_OPT',
                          'make install'],
                libs=['protobuf-c'],
                headers=['protobuf-c/protobuf-c.h'])

__all__ = ['define_components']
