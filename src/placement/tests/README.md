# Algorithmic Object Placement Unit Tests

This folder contains unit test utilities for DAOS's algorithmic object placement options.

These binaries are generated from this folder:
- `bin/ring_pl_test` - A unit test for `PL_TYPE_RING` placement. No command line arguments.
- `bin/jump_pl_test` - A unit test for `PL_TYPE_JUMP_MAP` placement.
  By default all the tests of a suite are run. A subset can be selected by index:

  ```bash
  jump_pl_map -L            # list the tests of the suite with their index
  jump_pl_map -u 1,2,5      # run only tests 1, 2 and 5
  jump_pl_map -u 2-8        # run tests 2 to 8
  jump_pl_map -p -u 0,3     # same, for the PDA suite (-p, -m, -d select other suites)
  ```

- `bin/pl_bench` - A tool to measure placement performance. Many different command-line arguments control cluster topology and what is measured.
