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

The ddb_test_driver.c file contains the entry point for ddb_tests. It does not
take any arguments for filtering or modifying tests; however, while debugging,
the "test_suites" and "cmocka_set_test_filter" variable and function can be
used, with a test recompile, to filter which tests are run.
