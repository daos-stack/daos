#!/usr/bin/python2
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

import os
import sys
import fnmatch
import subprocess
import jira
from jira import JIRA


def filelist(directory):
    """Create a list of test files contained in the provided path.

    This is meant to deal primarily with the structure of tests
    in the daos repo and would need to be changed to deal with
    random directory trees of tests.

    Args:
        directory (str): directory from which to create a list of files

    Returns:
        list: list of files in the directory

    """
    local_test_files = []
    test_pattern = "*.py"

    for path, _dirs, files in os.walk(directory):
        if not (path == directory or path == os.path.join(directory, 'util')):
            for test_file in files:
                if fnmatch.fnmatch(test_file, test_pattern):
                    local_test_files.append(os.path.join(path, test_file))
    return local_test_files


def yamlforpy(path):
    """Create the name of the yaml file for a given test file."""
    (base, _ext) = os.path.splitext(path)
    return base + ".yaml"


if __name__ == "__main__":

    # make it easy to specify a directory as a parameter later
    test_directory = os.getcwd()

    # build a list of test classes
    test_files = filelist(test_directory)

    tests = 0
    variants = 0
    print("working ")
    for _file in test_files:
        cmd1 = ["avocado", "list", _file]
        output = subprocess.check_output(cmd1)
        tests += len(output.splitlines())
        yamlfile = yamlforpy(_file)
        cmd2 = [
            "avocado", "variants", "-m", yamlfile, "--summary", "0",
            "--variants", "1"
        ]
        output = subprocess.check_output(cmd2)
        variants += len(output.splitlines())
        print(".")
        sys.stdout.flush()

    print("existing avocado tests> {}".format(tests))
    print("existing avocado variants> {}".format(variants))

    options = {'server': 'https://jira.hpdd.intel.com'}
    jira = JIRA(options)

    issues = jira.search_issues('project=DAOS AND component=test AND '
                                'assignee=daos-triage')
    print("test stories in backlog> {}".format(issues.total))

    q = """project=DAOS AND component=test AND project=DAOS AND
           status in ("In Progress","In Review")"""
    issues = jira.search_issues(q)

    print("total test stories in progress> {}".format(issues.total))

    q = """project=DAOS AND component=test AND project=DAOS AND
           status in ("Closed","Done","Resolved")"""
    issues = jira.search_issues(q)
    print("total test stories completed> {}".format(issues.total))
