#!/usr/bin/python
# Copyright (c) 2018-2021 Intel Corporation
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
'''
Run the raft tests using make -C DIR tests, where DIR is the path to the raft
Makefile. Check the output for the number of "not ok" occurrences and return
this number as the return code.
'''
import subprocess # nosec
import sys
import os
import json

# Get rid of complaints about parens for print statements and short var names
#pylint: disable=C0103
#pylint: disable=C0325

TEST_NOT_RUN = -1
DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'raft')

def number_of_failures():
    """
    Build and run the raft tests by calling the raft Makefile. Collect the
    output and examine for failures. If building or running fails, return
    TEST_NOT_RUN.
    """
    failures = 0
    successes = 0
    json_file = ".build_vars.json"
    path = os.path.join("build", DIR, "src")
    if os.path.exists(json_file):
        ofh = open(json_file, "r")
        conf = json.load(ofh)
        ofh.close()
        path = os.path.join(conf["BUILD_DIR"], DIR, "src")
    if not os.path.exists(path):
        try:
            res = subprocess.check_output(['/usr/bin/make', '-C', DIR, 'tests'])
        except Exception as e:
            print("Building Raft Tests failed due to\n{}".format(e))
            return TEST_NOT_RUN
    else:
        os.chdir(path)
        res = subprocess.check_output(["./tests_main"]).decode()

    for line in res.split('\n'):
        if line.startswith("not ok"):
            line = "FAIL: {}".format(line)
            failures += 1
        elif line.startswith("ok"):
            successes += 1
        print(line)

    if not successes and not failures:
        failures = TEST_NOT_RUN
    return failures

def main():
    """
    Run the raft tests, report success or failure, and cleanup the extraneous
    files generated by the raft Makefile.
    """
    failures = number_of_failures()
    if not failures:
        print("Raft Tests PASSED")
    elif failures == TEST_NOT_RUN:
        print("Raft Tests did not run")
    else:
        print("Raft Tests had {} failures".format(failures))
    sys.exit(failures)

if __name__ == "__main__":
    main()
