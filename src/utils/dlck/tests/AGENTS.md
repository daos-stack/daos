# DLCK Unit Test Instructions

Use `git commit --signoff` for every commit created for this work, including
amended commits, so each commit has a `Signed-off-by` trailer.

Apply these rules when adding or changing DLCK unit tests:

- Follow the repository `.clang-format` configuration for C/C++ source and
  test-table layout. Its current column limit is 100; count tabs as 8 columns.
- Test observable behavior: return codes, output, state transitions, boundary
  behavior, and resource cleanup. Do not reproduce the production algorithm in
  test assertions.
- Choose the smallest suitable suite and dependency boundary. Keep module tests
  separate from header-macro tests; add an integration case only when interaction
  between layers matters.
- Test `ck_common_printf()` in the lightweight common suite with a mock
  `ck_vprintf` callback; do not initialize main or worker checker state for this
  callback-dispatch contract.
- For each meaningful path, arrange inputs and CMocka expectations, invoke the
  production entry point, then assert the result and resulting state. Cover
  success and relevant failure boundaries, not every theoretical branch.
- Use `will_return(mock, value)` for each mocked return consumed by
  `mock_type(type)`; use `mock_ptr_type(type)` for pointer-returning mocks.
  Queue values in call order, including initialization and cleanup calls. Do
  not leave unused values or expectations. Use `EXPECT_CHECKER_D_CALLOC(Dcm)`
  or `EXPECT_CHECKER_D_CALLOC(Dcw)` for successful checker payload allocation.
  Use `expect_checker_d_calloc(sizeof(payload), NULL)` for allocation failure.
  The helper validates `nmemb` and `size`; every wrapped checker `d_calloc`
  call must have a matching expectation, or CMocka fails the test.
- Check arguments with matching CMocka pairs: `expect_value()` with
  `check_expected()`, `expect_string()` with `check_expected()`, and pointer
  expectations with `expect_value()` plus `check_expected_ptr()`. This CMocka
  version does not provide `expect_ptr()`.
- Strict mocks must validate every behavior-relevant argument on every call,
  including failure paths. Queue matching expectations per call; do not use
  allowlists or silently accept unexpected calls. Document intentionally ignored
  arguments.
- In particular, make `__wrap_d_free` call `check_expected_ptr(ptr)`
  unconditionally. Queue `expect_value(__wrap_d_free, ptr, expected_ptr)` before
  each free, including failure cleanup and temporary allocations such as
  `Mock_log_file`. This checks the argument, not general leak freedom.
- Use `assert_ptr_equal()` to verify resulting pointer state.
- Wrap only external calls whose results or arguments need control. Add the
  corresponding `-Wl,--wrap=<symbol>` and implement the exact
  `__wrap_<symbol>` signature. Use `__real_<symbol>` only for intentional
  pass-through behavior; strict unit-test wrappers must fail on unexpected
  calls rather than reaching real functions.
- Keep mocks local unless multiple suites need the same contract. Reset
  `mock_vfprintf_check_output` in per-test setup. The checker mock has no
  real-function fallbacks: `expect_checker_d_calloc()` queues the payload for
  an expected allocation, `__wrap_fopen` checks path and mode on every result,
  and the I/O wrappers check their stream arguments. `__wrap_vfprintf` always
  checks its format; `mock_vfprintf_check_output` enables rendered-output
  checking for that call.
- When an error path reads `errno`, set it in the relevant wrapper. Assert the
  correct mapping (`daos_errno2der()` or `dss_abterr2der()`) and cleanup.
- For DAOS assertions, call `d_register_alt_assert(mock_assert)` in `main()` before
  using `expect_assert_failure()`.
- Use fake streams only when real file contents are irrelevant, and ensure fake
  `FILE *` values never reach real stdio. Use real filesystem I/O when the test
  verifies file creation or contents, then clean up.
- Keep case IDs sequential (Starting from 101) in execution order in suites
  that use descriptive numbered IDs.
  - When adding a test binary, register it in `SConscript`, `utils/utest.yaml`, and
  `ci/test_files_to_stash.txt`. Use `env.Clone()`, a unique `OBJPREFIX`, and only
  the required sources, libraries, dependencies, include paths, and linker wraps.
- Build and run the affected binary. If shared checker mocks change, run both
  main and worker suites. From the DAOS repo root, the current debug/gcc pattern
  is:

  ```sh
  . ~/.venvs/daos/bin/activate
  ./utils/build/build_daos.sh --build-deps=no BUILD_TYPE=debug \
    build/debug/gcc/src/utils/dlck/tests/<binary>
  build/debug/gcc/src/utils/dlck/tests/<binary>
  git diff --check
  ```

- Treat build paths as configuration-dependent. Report gcov coverage only from a
  fresh instrumented build and matching `.gcno`/`.gcda`; do not infer current
  coverage from stale snapshots or macro-expansion branches.