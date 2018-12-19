#!/usr/bin/python
# Copyright (c) 2016-2018 Intel Corporation
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

from prereq_tools import GitRepoRetriever
from prereq_tools import WebRetriever
from prereq_tools import ProgramBinary
import os
import platform

if "prereqs" not in globals():
    from prereq_tools import PreReqComponent
    from SCons.Script import DefaultEnvironment
    from SCons.Script import Variables
    ENV = DefaultEnvironment()
    OPTS = Variables()
    REQS = PreReqComponent(ENV, OPTS)
else:
    REQS = globals()["prereqs"]

RT = ['rt']

if REQS.get_env('PLATFORM') == 'darwin':
    RT = []
else:
    REQS.define('rt', libs=['rt'])

REQS.define('cunit', libs=['cunit'], headers=['CUnit/Basic.h'],
            package='CUnit-devel')

REQS.define('python34_devel', headers=['python3.4m/Python.h'],
            package='python34-devel')

REQS.define('python27_devel', headers=['python2.7/Python.h'],
            package='python-devel')

REQS.define('libelf', headers=['libelf.h'], package='elfutils-libelf-devel')

REQS.define('tbbmalloc', libs=['tbbmalloc_proxy'], package='tbb-devel')

REQS.define('jemalloc', libs=['jemalloc'], package='jemalloc-devel')

REQS.define('boost', headers=['boost/preprocessor.hpp'], package='boost-devel')

REQS.define('yaml', headers=['yaml.h'], package='libyaml-devel')

REQS.define('event', libs=['event'], package='libevent-devel')

REQS.define('crypto', libs=['crypto'], headers=['openssl/md5.h'],
            package='openssl-devel')

if REQS.get_env('PLATFORM') == 'darwin':
    REQS.define('uuid', headers=['uuid/uuid.h'])
else:
    REQS.define('uuid', libs=['uuid'], headers=['uuid/uuid.h'],
                package='libuuid-devel')

REQS.define('openpa',
            retriever=GitRepoRetriever(
                'http://git.mcs.anl.gov/radix/openpa.git'),
            commands=['$LIBTOOLIZE', './autogen.sh',
                      './configure --prefix=$OPENPA_PREFIX', 'make $JOBS_OPT',
                      'make install'], libs=['opa'])

ISAL_BUILD = ['./autogen.sh ',
              './configure --prefix=$ISAL_PREFIX --libdir=$ISAL_PREFIX/lib',
              'make $JOBS_OPT', 'make install']

REQS.define('isal',
            retriever=GitRepoRetriever(
                'https://github.com/01org/isa-l.git'),
            commands=ISAL_BUILD,
            required_progs=['nasm', 'yasm'],
            libs=["isal"])

RETRIEVER = \
    GitRepoRetriever('https://github.com/mercury-hpc/mercury.git',
                     True)
REQS.define('mercury',
            retriever=RETRIEVER,
            commands=['cmake -DMERCURY_USE_CHECKSUMS=OFF '
                      '-DOPA_LIBRARY=$OPENPA_PREFIX/lib/libopa.a '
                      '-DOPA_INCLUDE_DIR=$OPENPA_PREFIX/include/ '
                      '-DCMAKE_INSTALL_PREFIX=$MERCURY_PREFIX '
                      '-DBUILD_EXAMPLES=OFF '
                      '-DMERCURY_USE_BOOST_PP=ON '
                      '-DMERCURY_USE_SELF_FORWARD=ON '
                      '-DMERCURY_ENABLE_VERBOSE_ERROR=ON '
                      '-DBUILD_TESTING=ON '
                      '-DNA_USE_OFI=ON '
                      '-DBUILD_DOCUMENTATION=OFF '
                      '-DBUILD_SHARED_LIBS=ON $MERCURY_SRC '
                      '-DCMAKE_INSTALL_RPATH=$MERCURY_PREFIX/lib '
                      '-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE '
                      '-DOFI_INCLUDE_DIR=$OFI_PREFIX/include '
                      '-DOFI_LIBRARY=$OFI_PREFIX/lib/libfabric.so'
                      , 'make $JOBS_OPT', 'make install'],
            libs=['mercury', 'na', 'mercury_util'],
            requires=['openpa', 'boost', 'ofi'] + RT,
            extra_include_path=[os.path.join('include', 'na')],
            out_of_src_build=True)

URL = 'https://www.open-mpi.org/software/hwloc/v1.11' \
    '/downloads/hwloc-1.11.5.tar.gz'
WEB_RETRIEVER = \
    WebRetriever(URL)
REQS.define('hwloc', retriever=WEB_RETRIEVER,
            commands=['./configure --prefix=$HWLOC_PREFIX', 'make $JOBS_OPT',
                      'make install'],
            headers=['hwloc.h'],
            libs=['hwloc'])

RETRIEVER = GitRepoRetriever('https://github.com/pmix/master')
REQS.define('pmix',
            retriever=RETRIEVER,
            commands=['./autogen.pl',
                      './configure --with-platform=optimized '
                      '--prefix=$PMIX_PREFIX '
                      '--with-hwloc=$HWLOC_PREFIX',
                      'make $JOBS_OPT', 'make install'],
            libs=['pmix'],
            required_progs=['autoreconf', 'aclocal', 'libtool'],
            headers=['pmix.h'],
            requires=['hwloc', 'event'])


RETRIEVER = GitRepoRetriever('https://github.com/pmix/prrte')
REQS.define('prrte',
            retriever=RETRIEVER,
            commands=['./autogen.pl',
                      './configure --with-platform=optimized '
                      '--prefix=$PRRTE_PREFIX '
                      '--enable-orterun-prefix-by-default '
                      '--with-pmix=$PMIX_PREFIX '
                      '--with-hwloc=$HWLOC_PREFIX',
                      'make $JOBS_OPT', 'make install'],
            required_progs=['g++', 'flex'],
            progs=['prun', 'prte', 'prted'],
            requires=['pmix', 'hwloc', 'event'])


RETRIEVER = GitRepoRetriever('https://github.com/open-mpi/ompi',
                             True)
REQS.define('ompi',
            retriever=RETRIEVER,
            commands=['./autogen.pl --no-oshmem',
                      './configure --with-platform=optimized '
                      '--enable-orterun-prefix-by-default '
                      '--prefix=$OMPI_PREFIX '
                      '--with-pmix=$PMIX_PREFIX '
                      '--disable-mpi-fortran '
                      '--enable-contrib-no-build=vt '
                      '--with-libevent=external '
                      '--with-hwloc=$HWLOC_PREFIX',
                      'make $JOBS_OPT', 'make install'],
            libs=['open-rte'],
            required_progs=['g++', 'flex'],
            requires=['pmix', 'hwloc', 'event'])


# Check if this is an ARM platform
PROCESSOR = platform.machine()
ARM_LIST = ["ARMv7", "armeabi", "aarch64", "arm64"]
ARM_PLATFORM = False
if PROCESSOR.lower() in [x.lower() for x in ARM_LIST]:
    ARM_PLATFORM = True

if ARM_PLATFORM:
    RETRIEVER = GitRepoRetriever("https://github.com/mercury-hpc/mchecksum.git")
    REQS.define('mchecksum',
                retriever=RETRIEVER,
                commands=['cmake -DBUILD_SHARED_LIBS=ON $MCHECKSUM_SRC'
                          '-DBUILD_TESTING=ON '
                          '-DCMAKE_INSTALL_PREFIX=$MCHECKSUM_PREFIX '
                          '-DMCHECKSUM_ENABLE_COVERAGE=OFF '
                          '-DMCHECKSUM_ENABLE_VERBOSE_ERROR=ON '
                          '-DMCHECKSUM_USE_ZLIB=OFF '
                          '-DCMAKE_INSTALL_RPATH=$MCHECKSUM_PREFIX/lib '
                          '-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE ',
                          'make $JOBS_OPT', 'make install'],
                libs=['mchecksum'],
                out_of_src_build=True)

RETRIEVER = GitRepoRetriever("https://github.com/pmem/pmdk.git")

PMDK_BUILD = ["make \"BUILD_RPMEM=n\" \"NDCTL_ENABLE=n\" \"NDCTL_DISABLE=y\" "
              "$JOBS_OPT install prefix=$PMDK_PREFIX"]

REQS.define('pmdk',
            retriever=RETRIEVER,
            commands=PMDK_BUILD,
            libs=["pmemobj"])

RETRIEVER = GitRepoRetriever("http://git.mcs.anl.gov/argo/argobots.git", True)
REQS.define('argobots',
            retriever=RETRIEVER,
            commands=['git clean -dxf ',
                      './autogen.sh',
                      './configure --prefix=$ARGOBOTS_PREFIX CC=gcc',
                      'make $JOBS_OPT',
                      'make $JOBS_OPT install'],
            libs=['abt'],
            headers=['abt.h'])

RETRIEVER = GitRepoRetriever("https://review.hpdd.intel.com/coral/cppr",
                             True)
REQS.define('cppr',
            retriever=RETRIEVER,
            commands=["scons $JOBS_OPT "
                      "OMPI_PREBUILT=$OMPI_PREFIX "
                      "CART_PREBUILT=$CART_PREFIX "
                      "FUSE_PREBUILT=$FUSE_PREFIX "
                      "IOF_PREBUILT=$IOF_PREFIX "
                      "PREFIX=$CPPR_PREFIX install"],
            headers=["cppr.h"],
            libs=["cppr"],
            requires=['iof', 'cart', 'ompi', 'fuse'])

RETRIEVER = GitRepoRetriever("https://review.hppd.intel.com/daos/iof",
                             True)
REQS.define('iof',
            retriever=RETRIEVER,
            commands=["scons $JOBS_OPT "
                      "OMPI_PREBUILT=$OMPI_PREFIX "
                      "CART_PREBUILT=$CART_PREFIX "
                      "FUSE_PREBUILT=$FUSE_PREFIX "
                      "PREFIX=$IOF_PREFIX install"],
            headers=['cnss_plugin.h'],
            requires=['cart', 'fuse', 'ompi'])

RETRIEVER = GitRepoRetriever("https://review.hpdd.intel.com/daos/daos_m",
                             True)
REQS.define('daos',
            retriever=RETRIEVER,
            commands=["scons $JOBS_OPT "
                      "OMPI_PREBUILT=$OMPI_PREFIX "
                      "CART_PREBUILT=$CART_PREFIX "
                      "PREFIX=$DAOS_PREFIX install"],
            headers=['daos.h'],
            requires=['cart', 'ompi'])

NINJA_PROG = ProgramBinary('ninja', ["ninja-build", "ninja"])

REQS.define('fuse',
            retriever=GitRepoRetriever('https://github.com/libfuse/libfuse'),
            commands=['meson $FUSE_SRC --prefix=$FUSE_PREFIX' \
                      ' -D udevrulesdir=$FUSE_PREFIX/udev' \
                      ' -D disable-mtab=True' \
                      ' -D utils=False',
                      '$ninja -v $JOBS_OPT',
                      '$ninja install'],
            libs=['fuse3'],
            defines=["FUSE_USE_VERSION=32"],
            required_progs=['libtoolize', NINJA_PROG],
            headers=['fuse3/fuse.h'],
            out_of_src_build=True)

REQS.define('ofi',
            retriever=GitRepoRetriever('https://github.com/ofiwg/libfabric'),
            commands=['./autogen.sh',
                      './configure --prefix=$OFI_PREFIX',
                      'make $JOBS_OPT',
                      'make install'],
            libs=['fabric'],
            headers=['rdma/fabric.h'])

RETRIEVER = GitRepoRetriever("https://review.hpdd.intel.com/daos/cart", True)
REQS.define('cart',
            retriever=RETRIEVER,
            commands=["scons $JOBS_OPT "
                      "OMPI_PREBUILT=$OMPI_PREFIX "
                      "MERCURY_PREBUILT=$MERCURY_PREFIX "
                      "PMIX_PREBUILT=$PMIX_PREFIX "
                      "PREFIX=$CART_PREFIX install"],
            headers=["cart/api.h", "gurt/list.h"],
            libs=["cart", "gurt"],
            requires=['mpi4py', 'mercury', 'uuid', 'crypto', 'ompi',
                      'pmix', 'boost', 'yaml'])

URL = 'https://bitbucket.org/mpi4py/mpi4py/downloads/mpi4py-2.0.0.tar.gz'
WEB_RETRIEVER = WebRetriever(URL)
REQS.define('mpi4py',
            retriever=WEB_RETRIEVER,
            commands=['python setup.py build --mpicc=$OMPI_PREFIX/bin/mpicc',
                      'python setup.py install --prefix $MPI4PY_PREFIX'],
            requires=['ompi'])

REQS.define('fio',
            retriever=GitRepoRetriever(
                'https://github.com/axboe/fio.git'),
            commands=['git checkout fio-3.3',
                      './configure --prefix="$FIO_PREFIX"',
                      'make $JOBS_OPT', 'make install'],
            progs=['genfio', 'fio'])

RETRIEVER = GitRepoRetriever("https://github.com/spdk/spdk.git", True)
REQS.define('spdk',
            retriever=RETRIEVER,
            commands=['./configure --prefix="$SPDK_PREFIX" ' \
                      ' --with-fio="$FIO_SRC"',
                      'make $JOBS_OPT', 'make install',
                      'mkdir -p "$SPDK_PREFIX/share/spdk"',
                      'cp -r  include scripts examples/nvme/fio_plugin ' \
                      '"$SPDK_PREFIX/share/spdk"'],
            libs=['spdk'],
            requires=['fio'])

URL = 'https://github.com/protobuf-c/protobuf-c/releases/download/' \
    'v1.3.0/protobuf-c-1.3.0.tar.gz'
WEB_RETRIEVER = WebRetriever(URL)
REQS.define('protobufc',
            retriever=WEB_RETRIEVER,
            commands=['./configure --prefix=$PROTOBUFC_PREFIX --disable-protoc',
                      'make $JOBS_OPT',
                      'make install'],
            libs=['protobuf-c'],
            headers=['protobuf-c/protobuf-c.h'])
