#!/bin/sh

SCRIPT_DIR=$(dirname $(readlink -f $0))
export PYTHONPATH=$SCRIPT_DIR:$SCRIPT_DIR/fake_scons:$PYTHONPATH

rm -f pylint.log
check_script ()
{
    pylint $* -d unused-wildcard-import >> tmp.log 2>&1
    grep rated tmp.log
    cat tmp.log >> pylint.log
    rm tmp.log
}
fail=0
while [ $# != 0 ]; do
    if [ "$1" = "-c" ]; then
        #Check our fake SCons module.  Doesn't have to
        #actually work so we turn off some warnings
        echo check SCons
        check_script SCons -d too-few-public-methods \
                           -d too-many-public-methods \
                           -d invalid-name \
                           -d unused-argument \
                           -d no-self-use
        echo check prereq_tools
        check_script prereq_tools
        echo check wrap_script.py
        check_script wrap_script.py
    elif [ "$1" = "-s" ]; then
        #Check a SCons file
        shift
        if [ ! -f $1 ]; then
        echo skipping non-existent file: $1
        fail=1
        else
        $SCRIPT_DIR/wrap_script.py $1
        echo check $1
        check_script "script"
        fi
    else
        if [ ! -f $1 ]; then
        echo skipping non-existent file: $1
        fail=1
        else
        echo check $1
        check_script $1
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
