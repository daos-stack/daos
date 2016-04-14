#!/bin/sh
./check_python.sh -c "components.py" \
                  -s "SConstruct" \
                  -s "test/SConstruct" \
                  -s "test/sl_test/SConscript"
exit $?
