#!/bin/sh

set -e
set -x

# Check for symbol names in the library.
if [ -d "utils" ]; then
  utils/test_cart_lib.sh
else
  ./test_cart_lib.sh
fi
# Run the tests from the install TESTING directory
if [ -z "$CART_TEST_MODE"  ]; then
  CART_TEST_MODE="native"
fi

if [ -n "$COMP_PREFIX"  ]; then
  TESTDIR=${COMP_PREFIX}/TESTING
else
  TESTDIR="install/Linux/TESTING"
fi
if [[ "$CART_TEST_MODE" =~ (native|all) ]]; then
  echo "Nothing to do yet, wish we could fail some tests"
  #scons utest
fi

if [[ "$CART_TEST_MODE" =~ (memcheck|all) ]]; then
  echo "Nothing to do yet"
  #scons utest --utest-mode=memcheck
fi
