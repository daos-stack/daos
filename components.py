#!/usr/bin/python
# Copyright (c) 2016 Intel Corporation
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
import os

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

REQS.define('tbbmalloc', libs=['tbbmalloc_proxy'], package='tbb-devel')

REQS.define('jemalloc', libs=['jemalloc'], package='jemalloc-devel')

REQS.define('boost', headers=['boost/preprocessor.hpp'], package='boost-devel')

REQS.define('event', libs=['event'], package='libevent-devel')

REQS.define('crypto', libs=['crypto'], headers=['openssl/md5.h'],
            package='openssl-devel')

if REQS.get_env('PLATFORM') == 'darwin':
    REQS.define('uuid', headers=['uuid/uuid.h'])
else:
    REQS.define('uuid', libs=['uuid'], headers=['uuid/uuid.h'],
                package='libuuid-devel')

CCI_BUILD = ['patch -N -p1 < $PATCH_PREFIX/cci_port_number.patch; '
             'if [ $? -gt 1 ]; then false; else true; fi;',
             'patch -N -p1 < $PATCH_PREFIX/cci_ib.patch; '
             'if [ $? -gt 1 ]; then false; else true; fi;',
             './autogen.pl']
CCI_REQUIRED = ['ltdl']
if REQS.get_env('PLATFORM') == 'darwin':
    CCI_BUILD.append('./configure --prefix=$CCI_PREFIX')
else:
    CCI_BUILD.append('./configure --with-verbs --prefix=$CCI_PREFIX')
    CCI_REQUIRED += ['rdmacm']
CCI_LIB = 'libcci$SHLIBSUFFIX'

CCI_BUILD += ['make', 'make install']
RETRIEVER = GitRepoRetriever('https://github.com/CCI/cci')
REQS.define('cci',
            retriever=RETRIEVER,
            commands=CCI_BUILD,
            required_libs=CCI_REQUIRED,
            required_progs=['cmake'],
            headers=['cci.h'],
            libs=["cci"])


REQS.define('openpa',
            retriever=GitRepoRetriever(
                'http://git.mcs.anl.gov/radix/openpa.git'),
            commands=['$LIBTOOLIZE', './autogen.sh',
                      './configure --prefix=$OPENPA_PREFIX', 'make',
                      'make install'], libs=['opa'])

# Save this old definition for now.
RETRIEVER = \
    GitRepoRetriever('https://github.com/mercury-hpc/mercury.git',
                     True)
REQS.define('mercury_old',
            retriever=RETRIEVER,
            commands=['cmake -DMCHECKSUM_USE_ZLIB=1 '
                      '-DOPA_LIBRARY=$OPENPA_PREFIX/lib/libopa.a '
                      '-DOPA_INCLUDE_DIR=$OPENPA_PREFIX/include/ '
                      '-DCCI_LIBRARY=$CCI_PREFIX/lib/%s '
                      '-DCCI_INCLUDE_DIR=$CCI_PREFIX/include/ '
                      '-DCMAKE_INSTALL_PREFIX=$MERCURY_OLD_PREFIX '
                      '-DBUILD_EXAMPLES=OFF '
                      '-DMERCURY_USE_BOOST_PP=ON '
                      '-DMERCURY_USE_SELF_FORWARD=ON'
                      '-DMERCURY_ENABLE_VERBOSE_ERROR=OFF '
                      '-DBUILD_TESTING=ON '
                      '-DNA_USE_CCI=ON '
                      '-DNA_CCI_USE_POLL=ON '
                      '-DBUILD_DOCUMENTATION=OFF '
                      '-DBUILD_SHARED_LIBS=ON $MERCURY_OLD_SRC '
                      '-DCMAKE_INSTALL_RPATH=$MERCURY_OLD_PREFIX/lib '
                      '-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE'
                      % (CCI_LIB), 'make', 'make install'],
            libs=['mercury', 'na', 'mercury_util', 'mchecksum'],
            requires=['openpa', 'cci', 'boost'] + RT,
            extra_include_path=[os.path.join('include', 'na')],
            out_of_src_build=True)

RETRIEVER = \
    GitRepoRetriever('https://github.com/mercury-hpc/mercury.git',
                     True, branch='topic_ofi')

REQS.define('mercury',
            retriever=RETRIEVER,
            commands=['cmake -DMCHECKSUM_USE_ZLIB=1 '
                      '-DOPA_LIBRARY=$OPENPA_PREFIX/lib/libopa.a '
                      '-DOPA_INCLUDE_DIR=$OPENPA_PREFIX/include/ '
                      '-DCCI_LIBRARY=$CCI_PREFIX/lib/%s '
                      '-DCCI_INCLUDE_DIR=$CCI_PREFIX/include/ '
                      '-DCMAKE_INSTALL_PREFIX=$MERCURY_PREFIX '
                      '-DBUILD_EXAMPLES=OFF '
                      '-DMERCURY_USE_BOOST_PP=ON '
                      '-DMERCURY_USE_SELF_FORWARD=ON'
                      '-DMERCURY_ENABLE_VERBOSE_ERROR=OFF '
                      '-DBUILD_TESTING=ON '
                      '-DNA_USE_CCI=ON '
                      '-DNA_CCI_USE_POLL=ON '
                      '-DNA_USE_OFI=ON '
                      '-DBUILD_DOCUMENTATION=OFF '
                      '-DBUILD_SHARED_LIBS=ON $MERCURY_SRC '
                      '-DCMAKE_INSTALL_RPATH=$MERCURY_PREFIX/lib '
                      '-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE '
                      '-DOFI_INCLUDE_DIR=$OFI_PREFIX/include '
                      '-DOFI_LIBRARY=$OFI_PREFIX/lib/libfabric.so'
                      % (CCI_LIB), 'make', 'make install'],
            libs=['mercury', 'na', 'mercury_util', 'mchecksum'],
            requires=['openpa', 'cci', 'boost', 'ofi'] + RT,
            extra_include_path=[os.path.join('include', 'na')],
            out_of_src_build=True)

# Duplicate for now to keep some new Jenkins jobs building.
REQS.define('mercury_ofi',
            retriever=RETRIEVER,
            commands=['cmake -DMCHECKSUM_USE_ZLIB=1 '
                      '-DOPA_LIBRARY=$OPENPA_PREFIX/lib/libopa.a '
                      '-DOPA_INCLUDE_DIR=$OPENPA_PREFIX/include/ '
                      '-DCCI_LIBRARY=$CCI_PREFIX/lib/%s '
                      '-DCCI_INCLUDE_DIR=$CCI_PREFIX/include/ '
                      '-DCMAKE_INSTALL_PREFIX=$MERCURY_OFI_PREFIX '
                      '-DBUILD_EXAMPLES=OFF '
                      '-DMERCURY_USE_BOOST_PP=ON '
                      '-DMERCURY_USE_SELF_FORWARD=ON'
                      '-DMERCURY_ENABLE_VERBOSE_ERROR=OFF '
                      '-DBUILD_TESTING=ON '
                      '-DNA_USE_CCI=ON '
                      '-DNA_CCI_USE_POLL=ON '
                      '-DNA_USE_OFI=ON '
                      '-DBUILD_DOCUMENTATION=OFF '
                      '-DBUILD_SHARED_LIBS=ON $MERCURY_OFI_SRC '
                      '-DCMAKE_INSTALL_RPATH=$MERCURY_OFI_PREFIX/lib '
                      '-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE '
                      '-DOFI_INCLUDE_DIR=$OFI_PREFIX/include '
                      '-DOFI_LIBRARY=$OFI_PREFIX/lib/libfabric.so'
                      % (CCI_LIB), 'make', 'make install'],
            libs=['mercury', 'na', 'mercury_util', 'mchecksum'],
            requires=['openpa', 'cci', 'boost', 'ofi'] + RT,
            extra_include_path=[os.path.join('include', 'na')],
            out_of_src_build=True)

URL = 'https://www.open-mpi.org/software/hwloc/v1.11' \
    '/downloads/hwloc-1.11.5.tar.gz'
WEB_RETRIEVER = \
    WebRetriever(URL)
REQS.define('hwloc', retriever=WEB_RETRIEVER,
            commands=['./configure --prefix=$HWLOC_PREFIX', 'make',
                      'make install'],
            headers=['hwloc.h'],
            libs=['hwloc'])

RETRIEVER = GitRepoRetriever('https://github.com/pmix/master')
REQS.define('pmix',
            retriever=RETRIEVER,
            commands=['./autogen.sh',
                      './configure --with-platform=optimized '
                      '--prefix=$PMIX_PREFIX '
                      '--with-hwloc=$HWLOC_PREFIX',
                      'make', 'make install'],
            libs=['pmix'],
            required_progs=['autoreconf', 'aclocal', 'libtool'],
            headers=['pmix.h'],
            requires=['hwloc', 'event'])

RETRIEVER = GitRepoRetriever('https://github.com/open-mpi/ompi')
REQS.define('ompi',
            retriever=RETRIEVER,
            commands=['patch -N -p1 < $PATCH_PREFIX/ompi_version.patch; '
                      'if [ $? -gt 1 ]; then false; else true; fi;',
                      './autogen.pl --no-oshmem',
                      './configure --with-platform=optimized '
                      '--enable-orterun-prefix-by-default '
                      '--prefix=$OMPI_PREFIX '
                      '--with-pmix=$PMIX_PREFIX '
                      '--disable-mpi-fortran '
                      '--with-libevent=external '
                      '--with-hwloc=$HWLOC_PREFIX',
                      'make', 'make install'],
            libs=['open-rte'],
            required_progs=['g++', 'flex'],
            requires=['pmix', 'hwloc', 'event'])

RETRIEVER = GitRepoRetriever('https://github.com/open-mpi/ompi')
REQS.define('ompi_pmix',
            retriever=RETRIEVER,
            commands=['patch -N -p1 < $PATCH_PREFIX/ompi_version.patch; '
                      'if [ $? -gt 1 ]; then false; else true; fi;',
                      './autogen.pl --no-oshmem',
                      './configure --with-platform=optimized '
                      '--with-devel-headers '
                      '--enable-orterun-prefix-by-default '
                      '--prefix=$OMPI_PMIX_PREFIX '
                      '--disable-mpi-fortran ',
                      'make', 'make install'],
            libs=['pmix'],
            libs_cc='$OMPI_PMIX_PREFIX/bin/mpicc',
            headers=['pmix.h'],
            required_progs=['g++', 'flex', 'autoreconf', 'aclocal', 'libtool'])

RETRIEVER = GitRepoRetriever("https://github.com/pmem/nvml")

REQS.define('nvml',
            retriever=RETRIEVER,
            commands=["make",
                      "make install prefix=$NVML_PREFIX"],
            libs=["pmemobj"])


RETRIEVER = GitRepoRetriever("http://git.mcs.anl.gov/argo/argobots.git", True)
REQS.define('argobots',
            retriever=RETRIEVER,
            commands=['git clean -dxf ',
                      './autogen.sh',
                      './configure --prefix=$ARGOBOTS_PREFIX',
                      'make -j4',
                      'make -j4 install'],
            libs=['abt'],
            headers=['abt.h'])

RETRIEVER = GitRepoRetriever("https://review.whamcloud.com/coral/cppr",
                             True)
REQS.define('cppr',
            retriever=RETRIEVER,
            commands=["scons "
                      "OMPI_PREBUILT=$OMPI_PREFIX "
                      "CART_PREBUILT=$CART_PREFIX "
                      "IOF_PREBUILT=$IOF_PREFIX "
                      "PREFIX=$CPPR_PREFIX install"],
            headers=["cppr.h"],
            libs=["cppr"],
            requires=['ompi', 'mercury', 'cart', 'iof'])

RETRIEVER = GitRepoRetriever("https://review.whamcloud.com/daos/iof",
                             True)
REQS.define('iof',
            retriever=RETRIEVER,
            commands=["scons "
                      "OMPI_PREBUILT=$OMPI_PREFIX "
                      "CART_PREBUILT=$CART_PREFIX "
                      "PREFIX=$IOF_PREFIX install"],
            headers=['cnss_plugin.h'],
            requires=['ompi', 'cart'])

REQS.define('fuse',
            retriever=GitRepoRetriever('https://github.com/libfuse/libfuse'),
            commands=['./makeconf.sh',
                      './configure --disable-util --prefix=$FUSE_PREFIX',
                      'make',
                      'make install'],
            libs=['fuse3'],
            defines=["FUSE_USE_VERSION=30"],
            required_progs=['libtoolize'],
            headers=['fuse3/fuse.h'])

REQS.define('ofi',
            retriever=GitRepoRetriever('https://github.com/ofiwg/libfabric'),
            commands=['./autogen.sh',
                      './configure --prefix=$OFI_PREFIX',
                      'make',
                      'make install'],
            libs=['fabric'],
            headers=['rdma/fabric.h'])

RETRIEVER = GitRepoRetriever("https://review.whamcloud.com/daos/cart", True)
REQS.define('cart',
            retriever=RETRIEVER,
            commands=["scons "
                      "ARGOBOTS_PREBUILT=$ARGOBOTS_PREFIX "
                      "OMPI_PREBUILT=$OMPI_PREFIX "
                      "MERCURY_PREBUILT=$MERCURY_PREFIX "
                      "PMIX_PREBUILT=$PMIX_PREFIX "
                      "PREFIX=$CART_PREFIX install"],
            headers=["crt_api.h"],
            libs=["crt", "crt_util"],
            requires=['ompi', 'mercury', 'pmix', 'argobots', 'crypto'])

URL = 'https://bitbucket.org/mpi4py/mpi4py/downloads/mpi4py-1.3.1.tar.gz'
WEB_RETRIEVER = WebRetriever(URL)
REQS.define('mpi4py',
            retriever=WEB_RETRIEVER,
            commands=['python setup.py build --mpicc=$OMPI_PREFIX/bin/mpicc',
                      'python setup.py install --prefix $MPI4PY_PREFIX'],
            requires=['ompi'])
