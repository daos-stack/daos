#!/usr/bin/env python3

#
# Copyright(c) 2012-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import subprocess
import sys
import os


args = ' '.join(sys.argv[1:])
script_path = os.path.dirname(os.path.realpath(__file__))
framework_script_path = os.path.join(script_path, "../framework/add_new_test_file.py")
framework_script_path = os.path.normpath(framework_script_path)
result = subprocess.run(framework_script_path + " " + args, shell=True,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
status = result.returncode
output = result.stdout.decode("ASCII", errors='ignore')

print(output)

if status == 0:
    path = output.split(" ", 1)[0]
    with open(script_path + os.sep + "header.c", "r") as header_file:
        with open(path, "r+") as source_file:
            source = source_file.readlines()

            source_file.seek(0, os.SEEK_SET)
            source_file.truncate()

            source_file.writelines(header_file.readlines())
            source_file.writelines(source)
