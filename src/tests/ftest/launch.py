#!/usr/bin/python2
'''
  (C) Copyright 2018 Intel Corporation.

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

import os
import time
import traceback
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

    test_files = []
    test_pattern = "*.py"

    for path, dirs, files in os.walk(directory):
        if not (path == directory or path.startswith(os.path.join(directory, 'util'))):
            for f in files:
                if fnmatch.fnmatch(f, test_pattern):
                    test_files.append(os.path.join(path,f))
    return test_files;


def yamlforpy(path):
    """
    Create the name of the yaml file for a given test file.
    """
    (base, ext) = os.path.splitext(path)
    return base + ".yaml"

def printhelp():
    """
    Print a list of test categories.
    """
    print("\n")
    print("Tests are launched by specifying a category.  For example:\n")
    print("\tbadconnect --run pool connect tests that pass NULL ptrs, etc.  ")
    print("\tbadevict --run pool client evict tests that pass "
          "NULL ptrs, etc.  ")
    print("\tbadexclude --run pool target exclude tests that "
          "pass NULL ptrs, etc.  ")
    print("\tbadparam --run tests that pass NULL ptrs, etc.  ")
    print("\tbadquery --run pool query tests that pass NULL ptrs, etc.  ")
    print("\tmulticreate --run tests that create multiple pools at once ")
    print("\tmultitarget --run tests that create pools over multiple servers")
    print("\tpool --run all pool related tests")
    print("\tpoolconnect --run all pool connection related tests")
    print("\tpooldestroy --run all pool destroy related tests")
    print("\tpoolevict --run all client pool eviction related tests")
    print("\tpoolinfo --run all pool info retrieval related tests")
    print("\tquick --run tests that complete quickly, with minimal resources ")
    print("\n")
    print("You can also specify the sparse flag -s to limit output to pass/fail.")
    print("Example command: launch.py -s pool")
    print("\n")
    exit()

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
                           "../../../.build_vars.json")) as f:
        envdata = json.load(f)

    BINDIR = envdata['PREFIX'] + '/bin'
    SBINDIR = envdata['PREFIX'] + '/sbin'
    PATH = os.environ.get('PATH')
    os.environ['PATH'] = BINDIR + ':' + SBINDIR + ':' + PATH
    os.environ['DAOS_SINGLETON_CLI'] = "1"
    os.environ['CRT_CTX_SHARE_ADDR'] = "1"
    os.environ['CRT_ATTACH_INFO_PATH'] = envdata['PREFIX'] + '/tmp'

    # build a list of test classes
    test_files = filelist(test_directory)
    if len(test_files) == 0:
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

    def run_test(_file, use_tags=True):
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
