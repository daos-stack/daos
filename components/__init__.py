#!/usr/bin/python
# Copyright (c) 2016-2019 Intel Corporation
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
from prereq_tools import GitRepoRetriever
from prereq_tools import WebRetriever
from prereq_tools import ProgramBinary

# Check if this is an ARM platform
PROCESSOR = platform.machine()
ARM_LIST = ["ARMv7", "armeabi", "aarch64", "arm64"]
ARM_PLATFORM = False
if PROCESSOR.lower() in [x.lower() for x in ARM_LIST]:
    ARM_PLATFORM = True

NINJA_PROG = ProgramBinary('ninja', ["ninja-build", "ninja"])

def inst(reqs, name):
    """Return True if name is in list of installed packages"""
    set([name, 'all']).intersection(set(reqs.installed))

def check(reqs, name, built_str, installed_str=""):
    """Return a different string based on whether a component is
       installed or not"""
    if inst(reqs, name):
        return installed_str
    return built_str

def define_mercury(reqs):
    """mercury definitions"""
    libs = ['rt']

    if reqs.get_env('PLATFORM') == 'darwin':
        libs = []
    else:
        reqs.define('rt', libs=['rt'])
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
                          '-DBUILD_TESTING=ON '
                          '-DNA_USE_OFI=ON '
                          '-DBUILD_DOCUMENTATION=OFF '
                          '-DBUILD_SHARED_LIBS=ON $MERCURY_SRC '
                          '-DCMAKE_INSTALL_RPATH=$MERCURY_PREFIX/lib '
                          '-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE ' +
                          check(reqs, 'ofi',
                                '-DOFI_INCLUDE_DIR=$OFI_PREFIX/include '
                                '-DOFI_LIBRARY=$OFI_PREFIX/lib/libfabric.so'),
                          'make $JOBS_OPT', 'make install'],
                libs=['mercury', 'na', 'mercury_util'],
                requires=['openpa', 'boost', 'ofi'] + libs,
                extra_include_path=[os.path.join('include', 'na')],
                out_of_src_build=True,
                package='mercury-devel' if inst(reqs, 'mercury') else None)

    retriever = GitRepoRetriever('https://github.com/ofiwg/libfabric')
    reqs.define('ofi',
                retriever=retriever,
                commands=['./autogen.sh',
                          './configure --prefix=$OFI_PREFIX',
                          'make $JOBS_OPT',
                          'make install'],
                libs=['fabric'],
                headers=['rdma/fabric.h'],
                package='libfabric-devel' if inst(reqs, 'ofi') else None)

    reqs.define('openpa',
                retriever=GitRepoRetriever(
                    'http://git.mcs.anl.gov/radix/openpa.git'),
                commands=['$LIBTOOLIZE', './autogen.sh',
                          './configure --prefix=$OPENPA_PREFIX',
                          'make $JOBS_OPT',
                          'make install'], libs=['opa'],
                package='openpa-devel' if inst(reqs, 'openpa') else None)

    if ARM_PLATFORM:
        url = "https://github.com/mercury-hpc/mchecksum.git"
        retriever = GitRepoRetriever(url)
        reqs.define('mchecksum',
                    retriever=retriever,
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


def define_common(reqs):
    """common system component definitions"""
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

    if reqs.get_env('PLATFORM') == 'darwin':
        reqs.define('uuid', headers=['uuid/uuid.h'])
    else:
        reqs.define('uuid', libs=['uuid'], headers=['uuid/uuid.h'],
                    package='libuuid-devel')

def define_pmix(reqs):
    """PMIX and related components"""
    url = 'https://www.open-mpi.org/software/hwloc/v1.11' \
        '/downloads/hwloc-1.11.5.tar.gz'
    web_retriever = \
        WebRetriever(url)
    reqs.define('hwloc', retriever=web_retriever,
                commands=['./configure --prefix=$HWLOC_PREFIX',
                          'make $JOBS_OPT', 'make install'],
                headers=['hwloc.h'],
                libs=['hwloc'],
                package='hwloc-devel' if inst(reqs, 'hwloc') else None)

    retriever = GitRepoRetriever('https://github.com/pmix/master')
    reqs.define('pmix',
                retriever=retriever,
                commands=['./autogen.pl',
                          './configure --with-platform=optimized '
                          '--prefix=$PMIX_PREFIX',
                          'make $JOBS_OPT', 'make install'],
                libs=['pmix'],
                required_progs=['autoreconf', 'aclocal', 'libtool'],
                headers=['pmix.h'],
                requires=['hwloc', 'event'],
                package='pmix-devel' if inst(reqs, 'pmix') else None)


    retriever = GitRepoRetriever('https://github.com/pmix/prrte')
    reqs.define('prrte',
                retriever=retriever,
                commands=['./autogen.pl',
                          './configure --with-platform=optimized '
                          '--prefix=$PRRTE_PREFIX '
                          '--enable-orterun-prefix-by-default ' +
                          check(reqs, 'pmix', '--with-pmix=$PMIX_PREFIX') +
                          ' ' +
                          check(reqs, 'hwloc', '--with-hwloc=$HWLOC_PREFIX'),
                          'make $JOBS_OPT', 'make install'],
                required_progs=['g++', 'flex'],
                progs=['prun', 'prte', 'prted'],
                requires=['pmix', 'hwloc', 'event'])


    retriever = GitRepoRetriever('https://github.com/open-mpi/ompi',
                                 True)
    reqs.define('ompi',
                retriever=retriever,
                commands=['./autogen.pl --no-oshmem',
                          './configure --with-platform=optimized '
                          '--enable-orterun-prefix-by-default '
                          '--prefix=$OMPI_PREFIX ' +
                          '--with-pmix=' +
                          check(reqs, 'pmix', '$PMIX_PREFIX', 'external') +
                          ' ' +
                          '--disable-mpi-fortran '
                          '--enable-contrib-no-build=vt '
                          '--with-libevent=external ' +
                          '--with-hwloc=' +
                          check(reqs, 'hwloc', '$HWLOC_PREFIX',
                                'external'),
                          'make $JOBS_OPT', 'make install'],
                libs=['open-rte'],
                required_progs=['g++', 'flex'],
                requires=['pmix', 'hwloc', 'event'],
                package='ompi-devel' if inst(reqs, 'ompi') else None)




def define_components(reqs):
    """Define all of the components"""
    define_common(reqs)
    define_mercury(reqs)
    define_pmix(reqs)

    isal_build = ['./autogen.sh ',
                  './configure --prefix=$ISAL_PREFIX --libdir=$ISAL_PREFIX/lib',
                  'make $JOBS_OPT', 'make install']
    reqs.define('isal',
                retriever=GitRepoRetriever(
                    'https://github.com/01org/isa-l.git'),
                commands=isal_build,
                required_progs=['nasm', 'yasm'],
                libs=["isal"])


    retriever = GitRepoRetriever("https://github.com/pmem/pmdk.git")

    pmdk_build = ["make \"BUILD_RPMEM=n\" \"NDCTL_ENABLE=n\" "
                  "\"NDCTL_DISABLE=y\" " "$JOBS_OPT install "
                  "prefix=$PMDK_PREFIX"]

    reqs.define('pmdk',
                retriever=retriever,
                commands=pmdk_build,
                libs=["pmemobj"])

    retriever = GitRepoRetriever("http://git.mcs.anl.gov/argo/argobots.git",
                                 True)
    reqs.define('argobots',
                retriever=retriever,
                commands=['git clean -dxf ',
                          './autogen.sh',
                          './configure --prefix=$ARGOBOTS_PREFIX CC=gcc',
                          'make $JOBS_OPT',
                          'make $JOBS_OPT install'],
                libs=['abt'],
                headers=['abt.h'])

    retriever = GitRepoRetriever("https://review.hpdd.intel.com/daos/iof",
                                 True)
    reqs.define('iof',
                retriever=retriever,
                commands=["scons $JOBS_OPT "
                          "OMPI_PREBUILT=$OMPI_PREFIX "
                          "CART_PREBUILT=$CART_PREFIX "
                          "FUSE_PREBUILT=$FUSE_PREFIX "
                          "PREFIX=$IOF_PREFIX "
                          "USE_INSTALLED=" + ','.join(reqs.installed) + ' ' +
                          "install"],
                headers=['cnss_plugin.h'],
                requires=['cart', 'fuse', 'ompi'])

    retriever = GitRepoRetriever("https://github.com/daos-stack/daos",
                                 True)
    reqs.define('daos',
                retriever=retriever,
                commands=["scons $JOBS_OPT "
                          "OMPI_PREBUILT=$OMPI_PREFIX "
                          "CART_PREBUILT=$CART_PREFIX "
                          "PREFIX=$DAOS_PREFIX "
                          "USE_INSTALLED=" + ','.join(reqs.installed) + ' ' +
                          "install"],
                headers=['daos.h'],
                requires=['cart', 'ompi'])

    retriever = GitRepoRetriever('https://github.com/libfuse/libfuse')
    reqs.define('fuse',
                retriever=retriever,
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

    retriever = GitRepoRetriever("https://github.com/daos-stack/cart",
                                 True)
    reqs.define('cart',
                retriever=retriever,
                commands=["scons $JOBS_OPT "
                          "OMPI_PREBUILT=$OMPI_PREFIX "
                          "MERCURY_PREBUILT=$MERCURY_PREFIX "
                          "PMIX_PREBUILT=$PMIX_PREFIX "
                          "PREFIX=$CART_PREFIX "
                          "USE_INSTALLED=" + ','.join(reqs.installed) + ' ' +
                          "install"],
                headers=["cart/api.h", "gurt/list.h"],
                libs=["cart", "gurt"],
                requires=['mercury', 'uuid', 'crypto', 'ompi',
                          'pmix', 'boost', 'yaml'],
                package='cart-devel' if inst(reqs, 'cart') else None)

    reqs.define('fio',
                retriever=GitRepoRetriever(
                    'https://github.com/axboe/fio.git'),
                commands=['git checkout fio-3.3',
                          './configure --prefix="$FIO_PREFIX"',
                          'make $JOBS_OPT', 'make install'],
                progs=['genfio', 'fio'])

    retriever = GitRepoRetriever("https://github.com/spdk/spdk.git", True)
    reqs.define('spdk',
                retriever=retriever,
                commands=['./configure --prefix="$SPDK_PREFIX" ' \
                          ' --with-fio="$FIO_SRC"',
                          'make $JOBS_OPT', 'make install',
                          'mkdir -p "$SPDK_PREFIX/share/spdk"',
                          'cp -r  include scripts examples/nvme/fio_plugin ' \
                          '"$SPDK_PREFIX/share/spdk"'],
                libs=['spdk'],
                requires=['fio'])

    url = 'https://github.com/protobuf-c/protobuf-c/releases/download/' \
        'v1.3.0/protobuf-c-1.3.0.tar.gz'
    web_retriever = WebRetriever(url)
    reqs.define('protobufc',
                retriever=web_retriever,
                commands=['./configure --prefix=$PROTOBUFC_PREFIX '
                          '--disable-protoc', 'make $JOBS_OPT',
                          'make install'],
                libs=['protobuf-c'],
                headers=['protobuf-c/protobuf-c.h'])

__all__ = ['define_components']
