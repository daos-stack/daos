#!/bin/sh
./check_python.sh -c "components.py" \
                  -s "SConstruct" \
                  -s "test/SConstruct" \
                  -s "test/SConstruct.alt"
exit $?
