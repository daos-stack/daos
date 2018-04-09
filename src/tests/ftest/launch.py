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

sys.path.append('./util')
import ServerUtils

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
        if not (path == directory or path == os.path.join(directory, 'util')):
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
    print("Tests are launched by specifying a category.  One of:\n")
    print("\tmulticreate --run tests that create multiple pools at once ")
    print("\tmultitarget --run tests that create pools over multiple servers")
    print("\tpool --run all pool related tests")
    print("\tpoolconnect --run all pool connection related tests")
    print("\tpooldestroy --run all pool destroy related tests")
    print("\tpoolevict --run all client pool eviction related tests")
    print("\tpoolinfo --run all pool info retrieval related tests")
    print("\tquick --run tests that complete quickly, with minimal resources ")
    print("\n")
    exit()

if __name__ == "__main__":

    # the caller supplies a test category or -h for help
    if len(sys.argv) == 2:
        if sys.argv[1] == '-h' or sys.argv[1] == '--help':
            printhelp()
        test_request = sys.argv[1]
    else:
        printhelp()

    # make it easy to specify a directory as a parameter later
    test_directory = os.getcwd()

    # build a list of test classes
    test_files = filelist(test_directory)
    if len(test_files) == 0:
        printhelp()

    avocado = ' avocado run'
    output_options = ' --show-job-log --html-job-result on'
    ignore_errors = ' --ignore-missing-references on'
    category = ' --filter-by-tags=' + test_request

    for f in test_files:
        param_file = yamlforpy(f)
        params = ' --mux-yaml ' + param_file

        test_cmd = avocado + ignore_errors + output_options +\
                   category + params + ' -- ' + f

        print("Running: " + test_cmd + "\n\n")
        subprocess.call(test_cmd, shell=True)
