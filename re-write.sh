#!/bin/sh

# ./utils/d_error_rewrite.py src/cart/crt_rpc.c

# diff src/cart/crt_rpc.c nf.txt

# exit 0

find src/client -name "*.[ch]" -a ! -name "*pb-c.[ch]" -exec ./utils/d_error_rewrite.py {} \;