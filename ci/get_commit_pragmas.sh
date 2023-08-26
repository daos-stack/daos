#!/bin/bash

set -eu
sed -ne '/^[^ ]*: */s/\([^:]*\): *\(.*\)/\1 "\2"/p' | while read a b; do
    echo -n "${a//-/_}" | tr [a-z] [A-Z]
    echo "=$b"
done
