#!/usr/bin/python2
'''
  (C) Copyright 2018-2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function

import os
import time
import sys
import fnmatch
import subprocess
import json

def filelist(directory):
    """
    Create a list of test files contained in the provided path.
    This is meant to deal primarily with the structure of tests
    in the daos repo and would need to be changed to deal with
    random directory trees of tests.
    """

    local_test_files = []
    test_pattern = "*.py"

    for path, _dirs, files in os.walk(directory):
        if not (path == directory or path.startswith(os.path.join(directory,
                                                                  'util'))):
            for test_file in files:
                if fnmatch.fnmatch(test_file, test_pattern):
                    local_test_files.append(os.path.join(path, test_file))
    return local_test_files

def yamlforpy(path):
    """
    Create the name of the yaml file for a given test file.
    """
    (base, _ext) = os.path.splitext(path)
    return base + ".yaml"

def printhelp():
    """
    Launch command.
    """
    print("\n")
    print("Usage: ./launch <tag>\n")
    print("\ttag:  ")
    print("\tall\tRun all tests ")
    print("\tproto\tRun basic CaRT protocol test ")
    print("\n")
    exit()

def run_test(_file, use_tags=True):
    """
    Launch a given test.
    """
    param_file = yamlforpy(_file)
    params = ' --mux-yaml ' + param_file
    test_cmd = avocado + ignore_errors + output_options
    if use_tags:
        test_cmd += category
    test_cmd += params + ' -- ' + _file

    start_time = int(time.time())
    print("Running: " + test_cmd + "\n\n")
    subprocess.call(test_cmd, shell=True)
    end_time = int(time.time())
    print("Total test run-time in seconds: {}".format(end_time - start_time))

if __name__ == "__main__":

    # not perfect param checking but good enough for now
    sparse = False
    addnl_tests = []
    if len(sys.argv) == 2:
        if sys.argv[1] == '-h' or sys.argv[1] == '--help':
            printhelp()
        test_request = sys.argv[1]
    elif len(sys.argv) >= 3:
        if sys.argv[1] == '-s':
            sparse = True
            test_request = sys.argv[2]
        if sparse:
            remaining_args = 3
        else:
            remaining_args = 2
        addnl_tests = sys.argv[remaining_args:]
    else:
        printhelp()

    # make it easy to specify a directory as a parameter later
    test_directory = os.getcwd()

    # setup some aspects of the environment
    with open(os.path.join(os.path.dirname(os.path.realpath(__file__)),
                           "./.build_vars.json")) as f:
        envdata = json.load(f)

    BINDIR = envdata['PREFIX'] + '/bin'
    SBINDIR = envdata['PREFIX'] + '/sbin'
    PATH = os.environ.get('PATH')
    os.environ['PATH'] = BINDIR + ':' + SBINDIR + ':' + PATH

    # build a list of test classes
    test_files = filelist(test_directory)
    if not test_files:
        printhelp()

    avocado = ' avocado run'
    if sparse:
        output_options = ' --html-job-result on'
    else:
        output_options = ' --show-job-log --html-job-result on'

    ignore_errors = ' --ignore-missing-references on'
    if test_request != 'all':
        category = ' --filter-by-tags=' + test_request
    else:
        category = ''

    # pylint: disable=unsupported-membership-test
    # run only provided tagged tests.
    for _file in test_files:
        if test_request == 'all':
            run_test(_file)
        else:
            list_cmd = 'avocado list {0} {1}'.format(category, _file)
            if _file in subprocess.check_output(list_cmd, shell=True):
                run_test(_file)

    # and explicitly listed tests.
    for _file in addnl_tests:
        run_test(_file, use_tags=False)
