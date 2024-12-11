#!/bin/bash
#
#  Copyright 2022-2024 Intel Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
# A git hook to validate and correct the copyright date in source files.

_print_githook_header "Copyright"
if [ -e .git/MERGE_HEAD ]; then
    echo "Merge commit. Skipping"
    exit 0
fi

echo "Updating copyright headers"

utils/cq/check_update_copyright.sh "$TARGET" githook
