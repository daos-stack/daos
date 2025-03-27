#!/bin/bash

#
# There are three different potential outputs on the distributions we test for;
# accept any.
#

LSPCI=../samples/lspci

if ! command -v lspci &> /dev/null
then
    exit 77
fi

test -n "$1" && LSPCI="$1"

$LSPCI | lspci -vv -F /dev/stdin >lspci.out

for i in 1 2 3; do
    if diff lspci.out $(dirname $0)/lspci.expected.out.$i >/dev/null 2>&1; then
        exit 0
    fi
done

# we don't match any; let's demonstrate one
diff lspci.out $(dirname $0)/lspci.expected.out.1 >&2

exit 1
