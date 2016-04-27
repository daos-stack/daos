#!/bin/sh

set -e
set -x

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

check_cmd()
{
    expected=$1
    shift
    $*
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
}

run_unit_tests()
{
    set +e
    failed=0
    check_cmd 'pass' scons -C test -f SConstruct.utest \
                           --test-name=leak
    check_cmd 'fail' scons -C test -f SConstruct.utest \
                           --test-name=leak --utest-mode=memcheck
    check_cmd 'pass' scons -C test -f SConstruct.utest \
                           --test-name=noleak --utest-mode=memcheck
    check_cmd 'pass' scons -C test -f SConstruct.utest \
                           --test-name=race
    check_cmd 'fail' scons -C test -f SConstruct.utest \
                           --test-name=race --utest-mode=helgrind
    check_cmd 'pass' scons -C test -f SConstruct.utest \
                           --test-name=norace --utest-mode=helgrind
    check_cmd 'fail' scons -C test -f SConstruct.utest \
                           --test-name=fail
    if [ $failed -ne 0 ]; then
    echo "Unit test failure"
    exit $failed
    else
    echo "All unit tests passed"
    fi
}

run_unit_tests
