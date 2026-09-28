# DLCK Unit Test Instructions

Follow [README.md](README.md) for rationale and examples. Apply these rules
when adding or changing DLCK unit tests:

- Test observable behavior: return codes, output, state transitions, boundary
  behavior, and resource cleanup. Do not reproduce the production algorithm in
  test assertions.
- Choose the smallest suitable suite and dependency boundary. Keep module tests
  separate from header-macro tests; add an integration case only when interaction
  between layers matters.
- For each meaningful path, arrange inputs and CMocka expectations, invoke the
  production entry point, then assert the result and resulting state. Cover
  success and relevant failure boundaries, not every theoretical branch.
- Use `will_return(mock, value)` for each mocked return consumed by
  `mock_type(type)`. Queue values in call order, including initialization and
  cleanup calls. Do not leave unused values or expectations.
- Check arguments with matching CMocka pairs: `expect_value()` with
  `check_expected()`, `expect_string()` with `check_expected()`, and pointer
  expectations with `expect_value()` plus `check_expected_ptr()`. This CMocka
  version does not provide `expect_ptr()`.
- Use `assert_ptr_equal()` to verify resulting pointer state. To verify checker
  cleanup, have the payload-specific `__wrap_d_free` path call
  `check_expected_ptr(ptr)` and queue `expect_value(__wrap_d_free, ptr, &Dcm)`
  or `&Dcw` before the operation that frees it. Queue expectations for failure
  paths that release an allocated payload too. Leave them absent when no free
  should occur; an unexpected tracked-payload free then fails in the wrapper.
  This checks the free call and pointer, not general leak freedom.
- Wrap only external calls whose results or arguments need control. Add the
  corresponding `-Wl,--wrap=<symbol>`, implement the exact `__wrap_<symbol>`
  signature, and declare/delegate to `__real_<symbol>` when needed. Pass through
  unrelated calls in broad wrappers.
- Keep mocks local unless multiple suites need the same contract. Reset every
  shared flag in per-test setup; make fake-resource behavior opt-in.
- When an error path reads `errno`, set it in the relevant wrapper. Assert the
  correct mapping (`daos_errno2der()` or `dss_abterr2der()`) and cleanup.
- For DAOS assertions, call `d_register_alt_assert(mock_assert)` in `main()` before
  using `expect_assert_failure()`.
- When testing formatted output or forwarding a `va_list`, check the format and
  rendered result. Use `va_copy()` before formatting the received list and
  `va_end()` on the copy.
- Use fake streams only when real file contents are irrelevant, and ensure fake
  `FILE *` values never reach real stdio. Use real filesystem I/O when the test
  verifies file creation or contents, then clean up.
- Keep case IDs sequential in execution order in suites that use descriptive
  numbered IDs. `cmocka_unit_test()` without IDs is appropriate for suites that
  do not use that convention.
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