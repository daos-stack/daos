#!/bin/bash
# Copyright (c) 2016-2018 Intel Corporation
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

  prebuilt1=()
  prebuilt2=()
  if [ -n "$PREBUILT_PREFIX" ]; then
    prebuilt1=( "PREBUILT_PREFIX=$PREBUILT_PREFIX" )
    openpa="${PREBUILT_PREFIX}/openpa"
    prebuilt2=( "OPENPA_PREBUILT=$openpa" )
  fi

  if [ -n "$SRC_PREFIX" ]; then
    prebuilt1+=( "SRC_PREFIX=$SRC_PREFIX" )
    prebuilt2+=( "SRC_PREFIX=$SRC_PREFIX" )
  fi

  mkdir -p test/prefix_test
  rm -rf test/prefix_test/*
  rm -f test/sl_test.info
  scons -C test -f SConstruct "${prebuilt1[@]}" --build-deps=yes --config=force
  python test/validate_build_info.py
  scons -C test -f SConstruct "${prebuilt2[@]}" --build-deps=yes --config=force
  python test/validate_build_info.py

  #Test clean
  scons -C test -f SConstruct "${prebuilt2[@]}" --build-deps=yes \
        --config=force -c
  python test/validate_build_info.py
  scons -C test -f SConstruct "${prebuilt2[@]}" --build-deps=yes --config=force
  python test/validate_build_info.py
  if scons -C test -f SConstruct "${prebuilt2[@]}" --build-deps=yes \
        --config=force --require-optional; then
      echo "Test for --require-optional failed"
      exit 1
  fi
fi

set +e
command -v clang-format >> /dev/null 2>&1
if [ "${PIPESTATUS[0]}" -eq 0 ]; then
    set -e
    directory=$(pwd)
    site_scons="${directory}/test/tool/site_scons"
    trap 'rm -f "${site_scons}"' EXIT
    ln -s "${directory}" "${site_scons}"
    scons -C test/tool -f SConstruct
fi
set -e

check_cmd()
{
    expected=$1
    issues=$2
    shift
    shift
    "$@" > scons_utest_output.txt 2>&1
    result=${PIPESTATUS[0]}
    if [ "$expected" = "pass" ]; then
        if [ "$result" -ne 0 ]; then
            failed=$((failed + 1))
        fi
    else
        if [ "$result" -eq 0 ]; then
            failed=$((failed + 1))
        fi
    fi
    grep "Valgrind.*check failed" scons_utest_output.txt
    result=${PIPESTATUS[0]}
    if [ "$issues" = "clean" ]; then
        if [ $result -eq 0 ]; then
            failed=$((failed + 1))
        fi
    else
        if [ $result -ne 0 ]; then
            failed=$((failed + 1))
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
