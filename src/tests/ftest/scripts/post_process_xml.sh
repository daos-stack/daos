#!/bin/bash
# /*
#  * (C) Copyright 2016-2023 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

set -eux

if [ "$#" -lt 1 ]; then
    echo "Failed to post-process XML. No component provided."
    exit
elif [ "$#" -lt 2 ]; then
    echo "No XML files to post-process for component $1."
    exit
fi

COMP="$1"

shift || true

FILES=("$@")

for file in "${FILES[@]}"; do
    if [ -f "$file" ]; then
        dir=$(dirname "$file")
        base=$(basename "$file")
        if [[ "${base}" =~ UTEST_|run_test\.sh ]]; then
            # for local runs, skip files already processed
            echo "Skipping ${file}"
            continue
        fi
        echo "Processing XML $file"

        if ! grep "<testcase classname" "$file" >/dev/null; then
            if ! SUITE=$(grep "testsuite " "$file" | \
                grep -Po "name=\"\K.*(?=\" time=)" | uniq); then
                echo "Failed to process XML $file. Cannot determine SUITE."
            else
                for suite_name in ${SUITE}; do
                    sed -i \
                    "s/case name/case classname=\"${COMP}.${suite_name}\" name/" "$file"
                done
            fi
        else
            sed -i "s/case classname=\"/case classname=\"${COMP}./" "$file"
        fi
        if grep "<testsuites>" "$file" >/dev/null; then
            sed -i "/<\/testsuites>/,/<testsuites>/ d" "$file"
            echo "</testsuites>" >> "$file"
        fi
        mv "$file" "${dir}/${COMP}_${base}"
    fi
done
