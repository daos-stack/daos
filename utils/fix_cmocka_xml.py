#!/usr/bin/env python
'''
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import sys

# Some fixes for the CMOCKA headers
xml_header = "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
parent_header = "<testsuites>\n"
parent_footer = "</testsuites>\n"
file_extensions = "*.xml"
path = os.path.join(os.getcwd(), "test_results") + os.path.sep
if not os.path.isdir(path):
    print("No test_results directory found")
    sys.exit()
files = [path+fn for fn in os.listdir(path) if any(fn.endswith(x) for x in file_extensions)]
# This is done because some XML files are not formed correctly
# by CMOCKA framework. Some files doesn't have <xml..> or
# <root> having nested tags [eg: if one group test called
# repeatedly].
# If there is a fix by CMOCKA framework, this is not required.
# Gitlab Issue: https://gitlab.com/cmocka/cmocka/issues/25
# Two pass update on the files
# Remove all testsuites tag out of the xml file.
for file in files:
    if os.path.isdir(file):
        print("Skipping %s, it's a directory" % file)
        continue
    if file != "":
        print(file)
        with open('{0}'.format(file), "r+") as file_handle:
            lines = file_handle.readlines()
            lines = [tmp.strip(' ') for tmp in lines]
            while parent_footer in lines:
                lines.remove(parent_footer)
            while parent_header in lines:
                lines.remove(parent_header)
            file_handle.truncate(0)
            file_handle.seek(0)
            file_handle.writelines(lines)
# Reconstruct the header and footer
for file in files:
    if os.path.isdir(file):
        print("Skipping %s, it's a directory" % file)
        continue
    if file != "":
        with open('{0}'.format(file), "r+") as file_handle:
            lines = file_handle.readlines()
            print(lines[0])
            if "xml" in lines[0]:
                lines[0] = lines[0].replace(lines[0], lines[0]+parent_header)
            else:
                line = lines[0], xml_header+parent_header+lines[0]
                lines[0] = lines[0].replace(line)
            file_handle.truncate(0)
            file_handle.seek(0)
            file_handle.writelines(lines)
            file_handle.seek(0, os.SEEK_END)
            file_handle.writelines(parent_footer)
