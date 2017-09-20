#!/bin/sh
# Copyright (c) 2016-2017 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

set -e
set -x

if [ "$1" != "utest" ]; then

  option=
  if [ -n "$WORKSPACE" ]; then
    prebuilt1=\
"PREBUILT_PREFIX=${CORAL_ARTIFACTS}/mercury-update-scratch/latest:\
${CORAL_ARTIFACTS}/ompi-update-scratch/latest"
    prebuilt2=\
"HWLOC_PREBUILT=${CORAL_ARTIFACTS}/ompi-update-scratch/latest/hwloc \
OPENPA_PREBUILT=${CORAL_ARTIFACTS}/mercury-update-scratch/latest/openpa"
  fi

  mkdir -p test/prefix_test
  rm -rf test/prefix_test/*
  rm -f test/sl_test.info
  scons -C test -f SConstruct $prebuilt1 --build-deps=yes --config=force
  python test/validate_build_info.py
  scons -C test -f SConstruct $prebuilt2 --build-deps=yes --config=force
  python test/validate_build_info.py

  #Test clean
  scons -C test -f SConstruct $prebuilt2 --build-deps=yes --config=force -c
  python test/validate_build_info.py
  scons -C test -f SConstruct $prebuilt2 --build-deps=yes --config=force
  python test/validate_build_info.py
  set +e
  scons -C test -f SConstruct $prebuilt2 --build-deps=yes --config=force \
        --require-optional
  if [ $? -eq 0 ]; then
      echo "Test for --require-optional failed"
      exit 1
  fi
  set -e
fi

check_cmd()
{
    expected=$1
    issues=$2
    shift
    shift
    $* > scons_utest_output.txt 2>&1
    result=$?
    if [ "$expected" = "pass" ]; then
        if [ $result -ne 0 ]; then
            failed=$[ $failed + 1 ]
        fi
    else
        if [ $result -eq 0 ]; then
            failed=$[ $failed + 1 ]
        fi
    fi
    grep "Valgrind.*check failed" scons_utest_output.txt
    result=$?
    if [ "$issues" = "clean" ]; then
        if [ $result -eq 0 ]; then
            failed=$[ $failed + 1 ]
        fi
    else
        if [ $result -ne 0 ]; then
            failed=$[ $failed + 1 ]
        fi
    fi
}

run_unit_tests()
{
    set +e
    failed=0
    check_cmd 'pass' 'clean' scons -C test -f SConstruct.utest \
                           --test-name=leak
    check_cmd 'pass' 'dirty' scons -C test -f SConstruct.utest \
                           --test-name=leak --utest-mode=memcheck
    check_cmd 'pass' 'clean' scons -C test -f SConstruct.utest \
                           --test-name=noleak --utest-mode=memcheck
    check_cmd 'pass' 'clean' scons -C test -f SConstruct.utest \
                           --test-name=race
    check_cmd 'pass' 'dirty' scons -C test -f SConstruct.utest \
                           --test-name=race --utest-mode=helgrind
    check_cmd 'pass' 'clean' scons -C test -f SConstruct.utest \
                           --test-name=norace --utest-mode=helgrind
    check_cmd 'fail' 'clean' scons -C test -f SConstruct.utest \
                           --test-name=fail
    if [ $failed -ne 0 ]; then
    echo "Unit test failure"
    exit $failed
    else
    echo "All unit tests passed"
    fi
}

run_unit_tests
