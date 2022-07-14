#!/bin/bash

set -e -x

rm -rf /tmp/vos.log
export D_LOG_FLUSH=debug
export D_LOG_MASK=debug
export DD_MASK=mem
export D_LOG_FILE=/tmp/vos.log
rm -rf /mnt/daos/*.vos
#export LD_LIBRARY_PATH=/home/jvolivie/daos/install/prereq/release/pmdk/lib/pmdk_debug:$LD_LIBRARY_PATH
VALGRIND="/opt/pmemcheck/bin/valgrind --tool=pmemcheck"

$VALGRIND ./vos -c foobar1 -d
$VALGRIND ./vos -o foobar1 -s -d
$VALGRIND ./vos -o foobar1 -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -w key1 -s -w key2 -s -w key3 -s -w key4 -s -w key5 -s -w key6 -s -w key7 -s -w key8 -s -w key9 -s -w key10 -s -d
$VALGRIND ./vos -o foobar1 -r -s -d
