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

if not "prereqs" in globals():
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

REQS.define('boost', headers=['boost/preprocessor.hpp'])

CCI_BUILD = ['patch -N -p1 < $PATCH_PREFIX/cci_port_number.patch; ' \
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
            retriever=GitRepoRetriever( \
            'http://git.mcs.anl.gov/radix/openpa.git'),
            commands=['$LIBTOOLIZE', './autogen.sh',
                      './configure --prefix=$OPENPA_PREFIX', 'make',
                      'make install'], libs=['opa'])

RETRIEVER = \
    GitRepoRetriever('https://github.com/mercury-hpc/mercury.git',
                     True)
REQS.define('mercury',
            retriever=RETRIEVER,
            commands=['cmake -DMCHECKSUM_USE_ZLIB=1 ' \
                      '-DOPA_LIBRARY=$OPENPA_PREFIX/lib/libopa.a ' \
                      '-DOPA_INCLUDE_DIR=$OPENPA_PREFIX/include/ ' \
                      '-DCCI_LIBRARY=$CCI_PREFIX/lib/%s ' \
                      '-DCCI_INCLUDE_DIR=$CCI_PREFIX/include/ ' \
                      '-DCMAKE_INSTALL_PREFIX=$MERCURY_PREFIX ' \
                      '-DBUILD_EXAMPLES=ON ' \
                      '-DMERCURY_USE_BOOST_PP=ON ' \
                      '-DMERCURY_ENABLE_VERBOSE_ERROR=OFF ' \
                      '-DBUILD_TESTING=ON ' \
                      '-DNA_USE_CCI=ON ' \
                      '-DBUILD_DOCUMENTATION=OFF ' \
                      '-DBUILD_SHARED_LIBS=ON $MERCURY_SRC ' \
                      '-DCMAKE_INSTALL_RPATH=$MERCURY_PREFIX/lib ' \
                      '-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE'
                      % (CCI_LIB), 'make', 'make install'],
            libs=['mercury', 'na', 'mercury_util', 'mchecksum'],
            requires=['openpa', 'boost', 'cci'] + RT,
            extra_include_path=[os.path.join('include', 'na')],
            out_of_src_build=True)

# pylint: disable=line-too-long
WEB_RETRIEVER = \
    WebRetriever('https://www.open-mpi.org/software/hwloc/v1.11/downloads/hwloc-1.11.2.tar.gz')
# pylint: enable=line-too-long
REQS.define('hwloc', retriever=WEB_RETRIEVER,
            commands=['./configure --prefix=$HWLOC_PREFIX', 'make',
                      'make install'],
            headers=['hwloc.h'],
            libs=['hwloc'])

REQS.define('pmix',
            retriever=GitRepoRetriever('https://github.com/pmix/master'),
            commands=['./autogen.sh',
                      './configure --with-platform=optimized ' \
                      '--prefix=$PMIX_PREFIX ' \
                      '--disable-visibility ' \
                      '--with-hwloc=$HWLOC_PREFIX',
                      'make', 'make install'],
            libs=['pmix'],
            required_libs=['event'],
            required_progs=['autoreconf', 'aclocal', 'libtool'],
            headers=['pmix.h'],
            requires=['hwloc'])

RETRIEVER = GitRepoRetriever('https://github.com/open-mpi/ompi')
REQS.define('ompi',
            retriever=RETRIEVER,
            commands=['patch -N -p1 < $PATCH_PREFIX/ompi_version.patch; ' \
                      'if [ $? -gt 1 ]; then false; else true; fi;',
                      './autogen.pl --no-oshmem',
                      './configure --with-platform=optimized ' \
                      '--enable-orterun-prefix-by-default ' \
                      '--prefix=$OMPI_PREFIX ' \
                      '--with-pmix=$PMIX_PREFIX ' \
                      '--disable-mpi-fortran ' \
                      '--with-libevent=external ' \
                      '--with-hwloc=$HWLOC_PREFIX',
                      'make', 'make install'],
            libs=['libopen-rte'],
            required_libs=['event'],
            required_progs=['g++', 'flex'],
            requires=['pmix', 'hwloc'])

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

RETRIEVER = GitRepoRetriever("http://review.whamcloud.com/coral/cppr",
                             True)
REQS.define('cppr',
            retriever=RETRIEVER,
            commands=["scons "
                      "PMIX_PREBUILT=$PMIX_PREFIX " \
                      "OMPI_PREBUILT=$OMPI_PREFIX " \
                      "HWLOC_PREBUILT=$HWLOC_PREFIX " \
                      "MERCURY_PREBUILT=$MERCURY_PREFIX " \
                      "CCI_PREBUILT=$CCI_PREFIX " \
                      "OPENPA_PREBUILT=$OPENPA_PREFIX " \
                      "PREFIX=$CPPR_PREFIX install"],
            headers=["cppr.h"],
            libs=["cppr"],
            requires=['ompi', 'mercury'])

RETRIEVER = GitRepoRetriever("http://review.whamcloud.com/daos/iof",
                             True)
REQS.define('iof',
            retriever=RETRIEVER,
            commands=["scons "
                      "PMIX_PREBUILT=$PMIX_PREFIX " \
                      "OMPI_PREBUILT=$OMPI_PREFIX " \
                      "HWLOC_PREBUILT=$HWLOC_PREFIX " \
                      "MERCURY_PREBUILT=$MERCURY_PREFIX " \
                      "MCL_PREBUILT=$MCL_PREFIX " \
                      "CCI_PREBUILT=$CCI_PREFIX " \
                      "OPENPA_PREBUILT=$OPENPA_PREFIX " \
                      "PREFIX=$IOF_PREFIX install"],
            headers=['iof_plugin.h'],
            requires=['ompi', 'mercury', 'mcl'])

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

RETRIEVER = GitRepoRetriever("http://review.whamcloud.com/daos/mcl", True)
REQS.define('mcl',
            retriever=RETRIEVER,
            commands=["scons "
                      "PMIX_PREBUILT=$PMIX_PREFIX " \
                      "OMPI_PREBUILT=$OMPI_PREFIX " \
                      "HWLOC_PREBUILT=$HWLOC_PREFIX " \
                      "MERCURY_PREBUILT=$MERCURY_PREFIX " \
                      "CCI_PREBUILT=$CCI_PREFIX " \
                      "OPENPA_PREBUILT=$OPENPA_PREFIX " \
                      "PREFIX=$MCL_PREFIX install"],
            headers=["process_set.h", "mcl_log.h", "mcl_event.h"],
            libs=["mcl"],
            requires=['ompi', 'mercury'])

RETRIEVER = GitRepoRetriever("http://review.whamcloud.com/daos/cart", True)
REQS.define('cart',
            retriever=RETRIEVER,
            commands=["scons "
                      "PMIX_PREBUILT=$PMIX_PREFIX " \
                      "ARGOBOTS_PREBUILT=$ARGOBOTS_PREFIX " \
                      "OMPI_PREBUILT=$OMPI_PREFIX " \
                      "HWLOC_PREBUILT=$HWLOC_PREFIX " \
                      "MERCURY_PREBUILT=$MERCURY_PREFIX " \
                      "CCI_PREBUILT=$CCI_PREFIX " \
                      "OPENPA_PREBUILT=$OPENPA_PREFIX " \
                      "PREFIX=$CART_PREFIX install"],
            headers=["crt_api.h"],
            libs=["crt"],
            requires=['ompi', 'mercury', 'argobots', 'pmix'])
