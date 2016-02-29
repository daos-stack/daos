#!/bin/sh

SCRIPT_DIR=$(dirname $(readlink -f $0))
SCONS_BIN=$(dirname $(readlink -f $(which scons)))
SCONS_LIB=$(dirname $SCONS_BIN)/lib/scons
export PYTHONPATH=.:$SCONS_LIB:$PYTHONPATH

rm -f pylint.log
check_script ()
{
    echo Checking $1
    pylint $1 >> tmp.log 2>&1
    grep rated tmp.log
    cat tmp.log >> pylint.log
    rm tmp.log
}

check_script "components.py"
check_script "prereq_tools"
check_script "SConstruct"
check_script "test/SConstruct"
check_script "test/SConstruct.alt"

echo "See pylint.log for details"
