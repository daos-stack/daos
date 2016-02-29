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
    __ENV__ = DefaultEnvironment()
    __OPTS__ = Variables()
    __REQS__ = PreReqComponent(__ENV__, __OPTS__)
else:
    __REQS__ = globals()["prereqs"]

__RT__ = ['rt']
__BMI_BUILD__ = ['./prepare']

if __REQS__.get_env('PLATFORM') == 'darwin':
    __RT__ = []
    __BMI_BUILD__.append('./configure --enable-bmi-only --prefix=$BMI_PREFIX')
    __BMI_LIB__ = 'libbmi.a'
else:
    __BMI_BUILD__.append('./configure --enable-shared --enable-bmi-only '\
                         '--prefix=$BMI_PREFIX')
    __BMI_LIB__ = 'libbmi$SHLIBSUFFIX'
    __REQS__.define('rt', libs=['rt'])

__BMI_BUILD__ += ['make', 'make install']

__REQS__.define('libevent', libs=['event'])

__REQS__.define('pthread', libs=['pthread'])

print __BMI_LIB__
__REQS__.define('bmi',
                retriever=GitRepoRetriever('http://git.mcs.anl.gov/bmi.git'),
                commands=__BMI_BUILD__, libs=['bmi'])

__REQS__.define('openpa',
                retriever=GitRepoRetriever( \
                'http://git.mcs.anl.gov/radix/openpa.git'),
                commands=['$LIBTOOLIZE', './autogen.sh',
                          './configure --prefix=$OPENPA_PREFIX', 'make',
                          'make install'], libs=['opa'])

__RETRIEVER__ = \
    GitRepoRetriever('ssh://review.whamcloud.com:29418/daos/mercury',
                     True)
__REQS__.define('mercury',
                retriever=__RETRIEVER__,
                commands=['cmake -DOPA_LIBRARY=$OPENPA_PREFIX/lib/libopa.a ' \
                          '-DOPA_INCLUDE_DIR=$OPENPA_PREFIX/include/ ' \
                          '-DBMI_LIBRARY=$BMI_PREFIX/lib/%s ' \
                          '-DBMI_INCLUDE_DIR=$BMI_PREFIX/include/ ' \
                          '-DCMAKE_INSTALL_PREFIX=$MERCURY_PREFIX ' \
                          '-DBUILD_EXAMPLES=ON ' \
                          '-DMERCURY_BUILD_HL_LIB=ON ' \
                          '-DMERCURY_USE_BOOST_PP=ON ' \
                          '-DNA_USE_BMI=ON -DBUILD_TESTING=ON ' \
                          '-DBUILD_DOCUMENTATION=OFF ' \
                          '-DBUILD_SHARED_LIBS=ON $MERCURY_SRC'
                          % __BMI_LIB__, 'make', 'make install'],
                libs=['mercury', 'na', 'mercury_util', 'mchecksum'],
                requires=['bmi', 'openpa', 'pthread'] + __RT__,
                extra_include_path=[os.path.join('include', 'na')],
                out_of_src_build=True)

__RETRIEVER__ = \
    WebRetriever('https://www.open-mpi.org/software/hwloc/v1.11/downloads/hwloc-1.11.2.tar.gz')
__REQS__.define('hwloc', retriever=__RETRIEVER__,
                commands=['./configure --prefix=$HWLOC_PREFIX', 'make',
                          'make install'], headers=['hwloc.h'])

__REQS__.define('pmix',
                retriever=GitRepoRetriever('https://github.com/pmix/master'),
                commands=['./autogen.sh',
                          './configure --with-platform=optimized ' \
                          '--prefix=$PMIX_PREFIX ' \
                          '--with-libevent=/usr --with-hwloc=$HWLOC_PREFIX',
                          'make', 'make install'],
                libs=['pmix'],
                headers=['pmix.h'],
                requires=['hwloc', 'libevent'])

__RETRIEVER__ = GitRepoRetriever('https://github.com/open-mpi/ompi')
__REQS__.define('ompi',
                retriever=__RETRIEVER__,
                commands=['./autogen.pl --no-ompi --no-oshmem',
                          './configure --prefix=$OMPI_PREFIX ' \
                          '--with-pmix=$PMIX_PREFIX ' \
                          '--disable-mpi-fortran --with-libevent=/usr ' \
                          '--with-hwloc=$HWLOC_PREFIX',
                          'make', 'make install'],
                libs=['libopen-rte'],
                requires=['pmix', 'hwloc', 'libevent'])
