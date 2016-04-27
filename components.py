#!/usr/bin/python
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
BMI_BUILD = ['./prepare']

if REQS.get_env('PLATFORM') == 'darwin':
    RT = []
    BMI_BUILD.append('./configure --enable-bmi-only --prefix=$BMI_PREFIX')
    BMI_LIB = 'libbmi.a'
else:
    BMI_BUILD.append('./configure --enable-shared --enable-bmi-only '\
                     '--prefix=$BMI_PREFIX')
    BMI_LIB = 'libbmi$SHLIBSUFFIX'
    REQS.define('rt', libs=['rt'])

BMI_BUILD += ['make', 'make install']

REQS.define('boost', headers=['boost/preprocessor.hpp'])

REQS.define('bmi',
            retriever=GitRepoRetriever('http://git.mcs.anl.gov/bmi.git'),
            commands=BMI_BUILD, libs=['bmi'])

CCI_BUILD = ['patch -p1 < $PATCH_PREFIX/cci_port_number.patch',
             './autogen.pl']
CCI_REQUIRED = []
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
            headers=['cci.h'],
            libs=["cci"])


REQS.define('openpa',
            retriever=GitRepoRetriever( \
            'http://git.mcs.anl.gov/radix/openpa.git'),
            commands=['$LIBTOOLIZE', './autogen.sh',
                      './configure --prefix=$OPENPA_PREFIX', 'make',
                      'make install'], libs=['opa'])

RETRIEVER = \
    GitRepoRetriever('ssh://review.whamcloud.com:29418/daos/mercury',
                     True)
REQS.define('mercury',
            retriever=RETRIEVER,
            commands=['cmake -DOPA_LIBRARY=$OPENPA_PREFIX/lib/libopa.a ' \
                      '-DOPA_INCLUDE_DIR=$OPENPA_PREFIX/include/ ' \
                      '-DBMI_LIBRARY=$BMI_PREFIX/lib/%s ' \
                      '-DBMI_INCLUDE_DIR=$BMI_PREFIX/include/ ' \
                      '-DCCI_LIBRARY=$CCI_PREFIX/lib/%s ' \
                      '-DCCI_INCLUDE_DIR=$CCI_PREFIX/include/ ' \
                      '-DCMAKE_INSTALL_PREFIX=$MERCURY_PREFIX ' \
                      '-DBUILD_EXAMPLES=ON ' \
                      '-DMERCURY_USE_BOOST_PP=ON ' \
                      '-DNA_USE_BMI=ON -DBUILD_TESTING=ON ' \
                      '-DNA_USE_CCI=ON ' \
                      '-DBUILD_DOCUMENTATION=OFF ' \
                      '-DBUILD_SHARED_LIBS=ON $MERCURY_SRC'
                      % (BMI_LIB, CCI_LIB), 'make', 'make install'],
            libs=['mercury', 'na', 'mercury_util', 'mchecksum'],
            requires=['bmi', 'openpa', 'boost', 'cci'] + RT,
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
                      '--enable-debug ' \
                      '--with-hwloc=$HWLOC_PREFIX',
                      'make', 'make install'],
            libs=['pmix'],
            required_libs=['event'],
            headers=['pmix.h'],
            requires=['hwloc'])

RETRIEVER = GitRepoRetriever('https://github.com/open-mpi/ompi')
REQS.define('ompi',
            retriever=RETRIEVER,
            commands=['./autogen.pl --no-oshmem',
                      './configure --with-platform=optimized ' \
                      '--prefix=$OMPI_PREFIX ' \
                      '--with-pmix=$PMIX_PREFIX ' \
                      '--disable-mpi-fortran ' \
                      '--with-libevent=external ' \
                      '--with-hwloc=$HWLOC_PREFIX',
                      'make', 'make install'],
            libs=['libopen-rte'],
            required_libs=['event'],
            requires=['pmix', 'hwloc'])

RETRIEVER = GitRepoRetriever("https://github.com/pmem/nvml")
REQS.define('nvml',
            retriever=RETRIEVER,
            commands=["make",
                      "make install prefix=$NVML_PREFIX"],
            libs=["pmemobj"])

RETRIEVER = GitRepoRetriever("ssh://review.whamcloud.com:29418/coral/cppr",
                             True)
REQS.define('cppr',
            retriever=RETRIEVER,
            commands=["scons --no-prereq-links "
                      "PMIX_PREBUILT=$PMIX_PREFIX " \
                      "OMPI_PREBUILT=$OMPI_PREFIX " \
                      "HWLOC_PREBUILT=$HWLOC_PREFIX " \
                      "MERCURY_PREBUILT=$MERCURY_PREFIX " \
                      "BMI_PREBUILT=$BMI_PREFIX " \
                      "CCI_PREBUILT=$CCI_PREFIX " \
                      "OPENPA_PREBUILT=$OPENPA_PREFIX " \
                      "PREFIX=$CPPR_PREFIX install"],
            headers=["cppr.h"],
            libs=["cppr"],
            requires=['ompi', 'mercury'])

RETRIEVER = GitRepoRetriever("ssh://review.whamcloud.com:29418/daos/iof",
                             True)
REQS.define('iof',
            retriever=RETRIEVER,
            commands=["scons --no-prereq-links "
                      "PMIX_PREBUILT=$PMIX_PREFIX " \
                      "OMPI_PREBUILT=$OMPI_PREFIX " \
                      "HWLOC_PREBUILT=$HWLOC_PREFIX " \
                      "MERCURY_PREBUILT=$MERCURY_PREFIX " \
                      "BMI_PREBUILT=$BMI_PREFIX " \
                      "CCI_PREBUILT=$CCI_PREFIX " \
                      "OPENPA_PREBUILT=$OPENPA_PREFIX " \
                      "PREFIX=$IOF_PREFIX install"],
            headers=["process_set.h"],
            libs=["pset"],
            requires=['ompi', 'mercury'])

REQS.define('fuse',
            retriever=GitRepoRetriever('https://github.com/libfuse/libfuse'),
            commands=['./makeconf.sh',
                      './configure --disable-util --prefix=$FUSE_PREFIX',
                      'make',
                      'make install'],
            libs=['fuse3'],
            defines=["FUSE_USE_VERSION=30"],
            headers=['fuse3/fuse.h'])

REQS.define('ofi',
            retriever=GitRepoRetriever('https://github.com/ofiwg/libfabric'),
            commands=['./autogen.sh',
                      './configure --prefix=$OFI_PREFIX',
                      'make',
                      'make install'],
            libs=['fabric'],
            headers=['rdma/fabric.h'])

RETRIEVER = GitRepoRetriever("ssh://review.whamcloud.com:29418/daos/mcl", True)
REQS.define('mcl',
            retriever=RETRIEVER,
            commands=["scons --no-prereq-links "
                      "PMIX_PREBUILT=$PMIX_PREFIX " \
                      "OMPI_PREBUILT=$OMPI_PREFIX " \
                      "HWLOC_PREBUILT=$HWLOC_PREFIX " \
                      "MERCURY_PREBUILT=$MERCURY_PREFIX " \
                      "BMI_PREBUILT=$BMI_PREFIX " \
                      "CCI_PREBUILT=$CCI_PREFIX " \
                      "OPENPA_PREBUILT=$OPENPA_PREFIX " \
                      "PREFIX=$MCL_PREFIX install"],
            headers=["process_set.h"],
            libs=["mcl"],
            requires=['ompi', 'mercury'])
