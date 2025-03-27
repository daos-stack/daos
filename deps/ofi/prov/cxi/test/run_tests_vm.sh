#!/bin/bash
#
# Run unit tests in a VM.

DIR=`dirname $0`
cd $DIR

./run.sh ./test.sh $1
