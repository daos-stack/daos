#!/bin/sh

set -eux

if [ "$#" -lt 1 ]; then
    echo "Failed to post-process XML. No component provided."
    exit
elif [ "$#" -lt 2 ]; then
    echo "No XML files to post-process for component $1"
    exit
fi

COMP="$1"
shift || true

FILES=("$@")

for file in "${FILES[@]}"
do
    if [ -f "$file" ]; then
        echo "Processing XML $file"

        if ! SUITE=$(grep "testsuite name=" "$file" | \
                grep -Po "name=\"\K.*(?=\" time=)"); then
                echo "Failed to process XML $file. Cannot determine SUITE."
        else
            if ! grep "<testcase classname" "$file"; then
                sed -i \
                "s/case name/case classname=\"${COMP}.${SUITE}\" name/" "$file"
            else
                sed -i "s/case classname=\"/case classname=\"${COMP}./" "$file"
            fi
        fi
    fi
done

