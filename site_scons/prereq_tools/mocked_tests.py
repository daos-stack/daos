#!/usr/bin/python
# Copyright 2017-2022 Intel Corporation
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
"""Methods for assembling a cmocka application from a set of c source files."""

import os
from os import path
import re
import glob
import shutil
from collections import namedtuple

# pylint: disable=too-few-public-methods

class TestFunction():
    """
    A simple store for tracking information about a specific test. Tied heavily
    to cmocka the function stores

    :string name The name of the test.
    :function setup The function to call before the test is run
    :function teardown The function to call after the test is run.
    """
    def __init__(self, name, setup, teardown):
        self.name = name
        self.setup = setup if setup != '' else 'NULL'
        self.teardown = teardown if teardown != '' else 'NULL'

    @property
    def description(self):
        """
        Produces a synthesized description of the test from the properties that
        we have.

        :return: string description of the test.
        """
        return '"%s"' % self.name


UnitTests = namedtuple('UnitTests',
                       ['sources',
                        'test_functions',
                        'global_setups',
                        'global_teardowns'])


def build_mock_unit_tests(env, src_list=None, **kwargs):
    """
    Call im place of Program with the same parameters other than the executable
    name and the source files.

    In concert with the _test_source_list this method searches the source
    directory for files called test_*.c if that
    file contains a test function (indicated using the UNIT_TEST macro to define
    the function) then it is added to the list of source files.

    In addition if the parent direction contains a file called XYZ.c (where the
    test file is called test_XYZ.c) then that file is included as well.

    A file called cmocka_tests.{hc} is created which defines all tests to be run
    by cmocka and finally an files in c_source are copied to the build directory
    for inclusion in the build.

    :param self: An SConsEnvironment.
    :param source: Additional source files that need to be added to the build.
    :param kwargs: Other arguments to be passed to Program

    :return: The return value from env.Program
    """
    src_list = src_list if src_list else []

    env['CPPPATH'].append(os.getcwd())

    kwargs['LIBS'] = kwargs.get('LIBS', [])
    kwargs['LIBS'].extend(['cmocka', 'mimick'])

    unit_tests = _get_source_and_tests(env, src_list)
    unit_tests = _create_source_files(unit_tests)

    env.Program('unit_test', list(unit_tests.sources),
                **kwargs)


def _parse_unit_tests(line, test_functions):
    """parse unit tests"""
    name = re.match("UNIT_TEST\\(([a-zA-Z0-9_, ]*)\\)", line)

    if name is not None:
        names = name.group(1).split(',') + ['', '']
        test_functions.append(
            TestFunction(names[0].strip(),
                         names[1].strip(),
                         names[2].strip()))

def _parse_global_setup(line, global_setups):
    """parse global setup"""
    name = re.match("GLOBAL_SETUP\\(([a-zA-Z0-9_ ]*)\\)", line)

    if name is not None:
        global_setups.append(name.group(1))


def _parse_global_teardowns(line, global_teardowns):
    """parse global teardowns"""
    name = re.match("GLOBAL_TEARDOWN\\(([a-zA-Z0-9_ ]*)\\)", line)

    if name is not None:
        global_teardowns.append(name.group(1))

def _get_source_and_tests(env, source_list):
    """get source and tests"""
    source_list = set(['cmocka_tests.c'] + source_list)

    test_functions = []
    global_setups = []
    global_teardowns = []

    for source_file in [file_obj.srcnode().get_abspath()
                        for file_obj in env.Glob('test_*.c')]:
        with open(source_file, 'r') as source:
            for line in source:
                _parse_unit_tests(line, test_functions)
                _parse_global_setup(line, global_setups)
                _parse_global_teardowns(line, global_teardowns)

                source_path = path.dirname(source_file)
                source_base = path.basename(source_file)

                source_list.add(source_base)

                tested_source_file = path.join(source_path, '..',
                                               source_base[len('test_'):])

                if path.isfile(tested_source_file):
                    potential_source_file = path.join('..',
                                                      path.basename(
                                                          tested_source_file))
                    source_list.add(potential_source_file)

    return UnitTests(source_list,
                     test_functions,
                     global_setups,
                     global_teardowns)


def _create_source_files(unit_tests):
    """Build the cmocka_test.{ch} source file"""
    tests = '\n'.join(['void %s(void **state);' % tf.name
                       for tf in unit_tests.test_functions])

    setups = '\n'.join(set('int %s(void **state);' % tf.setup
                           for tf in unit_tests.test_functions
                           if tf.setup != 'NULL'))

    teardowns = '\n'.join(set('int %s(void **state);' % tf.teardown
                              for tf in unit_tests.test_functions
                              if tf.teardown != 'NULL'))

    cmocka_tests = "{%s}" % ','.join(['\n{%s, %s, %s, %s, 0}' %
                                      (tf.description,
                                       tf.name,
                                       tf.setup,
                                       tf.teardown)
                                      for tf in unit_tests.test_functions])

    global_setups = '\n'.join('int %s(void **state);' % gs
                              for gs in unit_tests.global_setups)

    global_teardowns = '\n'.join('int %s(void **state);' % gt
                                 for gt in unit_tests.global_teardowns)

    test_source = """
// Auto generated file to run the cmocka tests

#include "unit_test.h"

// Test Functions
%s

// Setup Functions
%s

// Teardown Functions
%s

// Global Setup Functions
%s

// Global Teardown Functions
%s

static struct CMUnitTest generated_unit_tests[] = %s;

struct _cmocka_tests *
generated_cmocka_tests() {
        static struct _cmocka_tests cmocka_tests;

        cmocka_tests.group_name = "Unit Tests";
        cmocka_tests.tests = generated_unit_tests;
        cmocka_tests.number_of_tests = sizeof(generated_unit_tests) /
                                             sizeof(generated_unit_tests[0]);

        return &cmocka_tests;
}

int (*global_setup_functions[])(void **state) = {%s};
int (*global_teardown_functions[])(void **state) = {%s};

""" % (tests,
       setups,
       teardowns,
       global_setups,
       global_teardowns,
       cmocka_tests,
       ',\n'.join(unit_tests.global_setups + ['NULL']),
       ',\n'.join(unit_tests.global_teardowns + ['NULL']))

    py_source_path = path.dirname(path.abspath(__file__))

    try:
        with open('cmocka_tests.c', 'r') as original_test_source_file:
            original_test_source = original_test_source_file.read(1024*1024)
    except IOError:
        original_test_source = ""

    if original_test_source != test_source:
        with open('cmocka_tests.c', 'w') as test_source_file:
            test_source_file.write(test_source)

    for c_source_file in glob.glob(path.join(py_source_path,
                                             'c_source',
                                             '*')):
        c_dest_path = path.basename(c_source_file)
        if ((path.isfile(c_dest_path) is False) or
                (path.getmtime(c_source_file) > path.getmtime(c_dest_path))):
            shutil.copy(c_source_file, c_dest_path)

    for c_source_file in [path.basename(file_name)
                          for file_name in glob.glob(path.join(py_source_path,
                                                               'c_source',
                                                               '*'))]:
        if c_source_file.endswith('.c'):
            unit_tests.sources.add(c_source_file)

    return unit_tests
