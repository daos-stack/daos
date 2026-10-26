# Unit Test Instructions

## General Guidance

- Use `git commit --signoff` for every commit, including amended commits; each requires a
  `Signed-off-by` trailer.
- Follow `.clang-format` for C/C++ and test tables. The column limit is 100; count tabs as 8.

## Unit Tests

- Use the CMocka framework.
- Test observable behavior (return values, output, state, boundaries and cleanup).
- Don't reproduce the production algorithm in assertions.
- Choose the smallest suitable suite and dependency boundary.
- Separate module and header-macro tests.
- Add integration coverage only when interaction between layers matters.
- For meaningful paths, prepare inputs and expectations, call the production entry point, then
  assert result and state. Cover success and relevant failures, not every theoretical branch.
- Assert the correct error mapping and cleanup.
- Mock only external calls whose results or arguments need control. Add `-Wl,--wrap=<symbol>` and
  implement the exact `__wrap_<symbol>` signature.
- Register `d_register_alt_assert(mock_assert)` in `main()` before `expect_assert_failure()` for
  DAOS assertions.
- Keep numbered test IDs sequential from 101 in suites using descriptive IDs.
- Register new test binaries in `SConscript`, `utils/utest.yaml` and `ci/test_files_to_stash.txt`;
  keep sibling registrations alphabetical. Use `env.Clone()`, a unique `OBJPREFIX` when needed,
  and only required sources, libraries, dependencies, include paths and linker wraps.
- Set explicit `CPPPATH` for headers outside the default include paths.
- Build and run affected tests. For coverage, use a fresh instrumented build with matching
  `.gcno`/`.gcda`; do not infer coverage from stale data or macro-expansion branches alone.
- When a changed unit is used by more than one test all affected tests have to be run.

## Mocking Functions

- CMocka tests control the behavior of mocks by populating their respective queues with values.
  All of these values have to be later consumed by the mocks. CMocka queues are maintained per
  mock.
- Use `will_return(mock, value)` to queue test-controlled values for a mock.
- Mocks consume queued values by calling `mock_type(type)` for values and `mock_ptr_type(type)`
  for pointers.
- Queue only the values consumed along the tested path, including during initialization and
  cleanup; omit unused values.
- Do not hardcode values on the mock side.
- For pointer-returning I/O mocks such as `fopen`, queue the pointer or `NULL`.
- For APIs returning status and an output handle, queue the handle only on success.
- For mocks which are expected to set `errno` on error, queue an `errno` only on error.
- Successful free APIs may have fixed output effects, such as clearing a handle.

## Checking Parameters

- CMocka tests check parameters received by mocks by expressing expectations. The mock has to call
  a dedicated `check_*` variant.
- For values: `expect_value()` with `check_expected()`.
- For strings: `expect_string()` with `check_expected()`.
- For pointers: `expect_value()` with `check_expected_ptr()`. This CMocka version does not provide
  `expect_ptr()`.

## Strict Mocks

- Always use strict mocks only.
- Especially: `__wrap_d_calloc()` and `__wrap_d_free()` if required by the test.
- For I/O mocks check also printf-style formats.
- Never use `__real_*()` variants.
- Never pass fake `FILE *` values to real stdio.

## DLCK Checker Tests

- Keep checker suites at the smallest useful boundary:
  - `dlck_checker_common_ut.c` tests `ck_common_printf()` with a lightweight `ck_vprintf` callback.
    Do not initialize main or worker state for this callback-dispatch contract.
  - `dlck_checker_main_ut.c` and `dlck_checker_worker_ut.c` compile the checker implementation
    into separate binaries with shared `dlck_checker_ut_mock.c`.
- Use `EXPECT_CHECKER_D_CALLOC(Dcm)` or `EXPECT_CHECKER_D_CALLOC(Dcw)` for successful payload
  allocation; use `expect_checker_d_calloc(sizeof(payload), NULL)` for failure.
- Reset `mock_vfprintf_check_output` in per-test setup. `__wrap_vfprintf` always checks its format;
  set the flag to also check rendered `va_list` output. Pair `va_copy()` with `va_end()` when
  formatting a copied `va_list`.
- From the DAOS root, build/run a debug/gcc suite with:

  ```sh
  ./utils/build/build_daos.sh --build-deps=no BUILD_TYPE=debug \
    build/debug/gcc/src/utils/dlck/tests/<binary>
  build/debug/gcc/src/utils/dlck/tests/<binary>
  ```

  - Use Python virtual environment for `build` step
