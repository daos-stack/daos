# ddb_tests

The ddb_tests executable will test the ddb utility at multiple levels. There is
a different test suite for each of the following layers:

- The parsing tests unit test various utility type functions that are used for
  parsing input.
- The cmd options tests unit test that the 'getopt_long' function parameters are
  setup correctly for each of the commands.
- The vos interface layer is tested with a vos instance so a mount point must be
  setup correctly at /mnt/daos with tmpfs.
- The commands tests verify that the command functions work correctly. Even
  though test suite will setup a vos instance and tests the commands and vos
  layers together, most of the testing is focused on the commands layer, that
  invalid input for options and arguments is handled appropriately, etc.
- The main test suite focuses on the ddb_main function and that the ddb utility
  options and arguments are handled appropriately.
- The print test suite looks at how information is printed and if it seems
  correct.

The ddb_test_driver.c file contains the entry point for ddb_tests. It accepts
the following options:

- `-s`, `--suites <letters>` runs only the test groups listed by their letter:
  `a` parsing, `b` vos, `c` commands, `d` print and `e` path. All the groups
  run by default.
- `-f`, `--filter <test>` runs only the tests matching the cmocka filter. It
  needs a build with `CMOCKA_FILTER_SUPPORTED=1` (cmocka 1.1.5 or later);
  otherwise ddb_tests prints "Test filtering disabled at compile time." and
  runs every test.
- `-c`, `--create_vos` only creates the VOS file used by the tests, filled with
  a few containers, objects and keys (`/mnt/daos/<pool uuid>/vos-0`), prints
  its path and exits without running any test.
