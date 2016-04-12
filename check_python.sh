#!/bin/sh

SCRIPT_DIR=$(dirname $0)
export PYTHONPATH=$SCRIPT_DIR:$SCRIPT_DIR/fake_scons:$PYTHONPATH
rm -f pylint.log

fail=0
while [ $# != 0 ]; do
    if [ "$1" = "-c" ]; then
        #Run the self check on prereq_tools and various
        #helper scripts
        echo Run self check
        $SCRIPT_DIR/check_script.py -s
    elif [ "$1" = "-s" ]; then
        #Check a SCons file
        shift
        if [ ! -f $1 ]; then
            echo skipping non-existent file: $1
            fail=1
        else
            echo check $1
            $SCRIPT_DIR/check_script.py -w $1
        fi
    else
        if [ ! -f $1 ]; then
            echo skipping non-existent file: $1
            fail=1
        else
            echo check $1
            $SCRIPT_DIR/check_script.py $1
        fi
    fi
    shift
done

echo "See pylint.log for detailed report"
list=`grep rated pylint.log | grep -v "rated at 10"`
if [ $fail -eq 1 ] || [ "$list" != "" ]; then
echo Fail
exit 1
fi
exit 0
