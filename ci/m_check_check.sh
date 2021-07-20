#!/bin/sh

set -e

cp ci/d_check.c ci/d_check.c.new

./ci/check_d_macro_calls.py ci/d_check.c.new | patch -p1
./ci/check_d_macro_calls.py ci/d_check.c.new | patch -p1
./ci/check_d_macro_calls.py ci/d_check.c.new | patch -p1

diff ci/d_check.c.new ci/d_check_post.c

echo Files are the same.
