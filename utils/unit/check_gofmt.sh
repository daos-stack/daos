#!/bin/bash

set -ue

echo Checking go formatting

output=$(find src -name '*.go' -and -not -path '*vendor*' -print0 | xargs -0 gofmt -d)

if [ -n "$output"  ]
then
        echo "ERROR: Your code hasn't been run through gofmt!
Please configure your editor to run gofmt on save.
Alternatively, at a minimum, run the following command:
find src -name '*.go' -and -not -path '*vendor*' | xargs gofmt -w

gofmt check found the following:

$output
"
        exit 1
fi
