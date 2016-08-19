#!/bin/sh
./check_python.sh -c "components.py" \
                  -s "SConstruct" \
                  -s "test/SConstruct.utest" \
                  -s "test/SConstruct" \
                  -s "test/sl_test/SConscript" \
                  -s "test/utest/SConscript" \
                  -P3 "test_runner/__main__.py" \
                  -P3 "test_runner/TestRunner.py" \
                  -P3 "test_runner/InfoRunner.py" \
                  -P3 "test_runner/DvmRunner.py"
exit $?
