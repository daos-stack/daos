# Unit Test Instructions

## General Guidance

- Use `git commit --signoff` for every commit, including amended commits; each requires a
  `Signed-off-by` trailer.
- Follow `.clang-format` for C/C++ and test tables. The column limit is 100; count tabs as 8.
- Test observable behavior (return values, output, state, boundaries and cleanup); don't reproduce
  the production algorithm in assertions.
- Choose the smallest suitable suite and dependency boundary. Separate module and header-macro
  tests; add integration coverage only when interaction between layers matters.
- For meaningful paths, prepare inputs and expectations, call the production entry point, then
  assert result and state. Cover success and relevant failures, not every theoretical branch.
- `will_return(mock, value)` pairs with `mock_type(type)`; use `mock_ptr_type(type)` for pointer
  returns.
- Queue test-controlled values in each test, not as mock constants. Queue only values consumed on
  that path, including initialization and cleanup; leave none unused. CMocka queues are per mock,
  so passing tests do not prove separate mocks follow production call order.
- Pair argument expectations with checks: `expect_value()` with `check_expected()`,
  `expect_string()` with `check_expected()`, and pointer checks with `expect_value()` plus
  `check_expected_ptr()`. This CMocka version does not provide `expect_ptr()`.
- For tracked allocations, make `__wrap_d_free` check its pointer and queue an expectation before
  each expected free. This verifies the free call and pointer, not general leak freedom.
- Strict allocator mocks such as `d_calloc` need an expectation for every call, including size and
  result. Strict mocks must check every behavior-relevant argument, including on failure; don't
  silently accept unexpected calls, and document intentionally ignored arguments.
- Validate I/O mock arguments on every call, including file paths, modes and printf-style formats.
- Wrap only external calls whose results or arguments need control. Add `-Wl,--wrap=<symbol>` and
  implement the exact `__wrap_<symbol>` signature. Use `__real_<symbol>` only for intentional
  pass-through; strict mocks should fail on unexpected calls rather than reach real functions.
- For pointer-returning I/O wrappers such as `fopen`, queue the pointer or `NULL` and check relevant
  arguments. For APIs returning status plus an output handle, queue the status and queue the handle
  only on success. Successful free APIs may have fixed output effects, such as clearing a handle;
  check their input arguments.
- When an error path reads `errno`, queue the failing mock result followed by errno; consume errno
  only on failure. Assert the correct error mapping and cleanup.
- Register `d_register_alt_assert(mock_assert)` in `main()` before `expect_assert_failure()` for
  DAOS assertions.
- Use fake streams only when real contents are irrelevant; never pass fake `FILE *` values to real
  stdio. Use real filesystem I/O when testing file creation or contents, then clean it up.
- Keep numbered test IDs sequential from 101 in suites using descriptive IDs.
- Register new test binaries in `SConscript`, `utils/utest.yaml` and `ci/test_files_to_stash.txt`;
  keep sibling registrations alphabetical. Use `env.Clone()`, a unique `OBJPREFIX` when needed,
  and only required sources, libraries, dependencies, include paths and linker wraps.
- Build and run affected tests. For coverage, use a fresh instrumented build with matching
  `.gcno`/`.gcda`; do not infer coverage from stale data or macro-expansion branches alone.

## DLCK Checker Tests

- Keep checker suites at the smallest useful boundary:
  - `dlck_checker_common_ut.c` tests `ck_common_printf()` with a lightweight `ck_vprintf` callback.
    Do not initialize main or worker state for this callback-dispatch contract.
  - `dlck_checker_main_ut.c` and `dlck_checker_worker_ut.c` compile the checker implementation
    into separate binaries with shared `dlck_checker_ut_mock.c`.
- Use `EXPECT_CHECKER_D_CALLOC(Dcm)` or `EXPECT_CHECKER_D_CALLOC(Dcw)` for successful payload
  allocation; use `expect_checker_d_calloc(sizeof(payload), NULL)` for failure. The helper checks
  `nmemb` and `size`.
- Reset `mock_vfprintf_check_output` in per-test setup. `__wrap_vfprintf` always checks its format;
  set the flag to also check rendered `va_list` output. Pair `va_copy()` with `va_end()` when
  formatting a copied `va_list`.
- The test helper needs explicit `CPPPATH` to find `engine/srv_internal.h`.
- From the DAOS root, build/run a debug/gcc suite with:

  ```sh
  . ~/.venvs/daos/bin/activate
  ./utils/build/build_daos.sh --build-deps=no BUILD_TYPE=debug \
    build/debug/gcc/src/utils/dlck/tests/<binary>
  build/debug/gcc/src/utils/dlck/tests/<binary>
  git diff --check
  ```
- If the shared checker mock changes, run both main and worker suites.
