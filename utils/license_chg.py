#!/usr/bin/python3
"""
   (C) Copyright 2020 Intel Corporation.

   SPDX-License-Identifier: BSD-2-Clause-Patent

   Python tool to replace license text in various source code files.  Delete this
   file once license change is complete.
"""

import argparse, pathlib


if __name__ == "__main__" :

    parser = argparse.ArgumentParser()
    parser.add_argument('file', type=pathlib.Path)
    args = parser.parse_args()
    its_go = False

    if str(args.file).endswith('.c') or str(args.file).endswith('.h'):
        new_license = "* SPDX-License-Identifier: BSD-2-Clause-Patent\n"
        old_license = "* Licensed under the Apache"
        end_license_section = " */"
    elif str(args.file).endswith('.py'):
        new_license = "SPDX-License-Identifier: BSD-2-Clause-Patent\n"
        old_license = "Licensed under the Apache"
        end_license_section = tuple(('"""', "'''"))
    elif str(args.file).endswith('.go'):
        its_go = True
        new_license = "// SPDX-License-Identifier: BSD-2-Clause-Patent\n"
        old_license = "// Licensed under the Apache"
        end_license_section = '//'
    else:
        print("ERROR: File {} is not something I know how to handle, did nothing.".format(args.file))

    srcfile = args.file.open('r+')
    lines = srcfile.readlines()
    srcfile.close()

    i = 0
    start = -1
    end = -1
    leading_space = 0

    for line in lines:
        if line.lstrip().startswith(old_license):
            leading_space = len(line) - len(line.lstrip())
            lines[i] = ' '*leading_space + new_license
            start = i+1
        elif start > 0 and not its_go and line.startswith(end_license_section):
            end = i
            break
        elif start > 0 and its_go and end_license_section not in line:
            end = i+1
            break
        i = i+1

    if start >= 0 and end >= 0:
        del lines[start:end]

        srcfile = args.file.open('w')
        srcfile.writelines(lines)
        srcfile.close()
        print("{} updated".format(args.file))
    else:
        print("ERROR: Something didn't look right with file {} so I did nothing".format(args.file))
