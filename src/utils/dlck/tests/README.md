# Writing DLCK unit tests

Use CMocka to test the observable behavior of a DLCK module: outputs, state changes,
error codes, cleanup, and calls across its boundaries. Do not reproduce the algorithm
under test in the assertions. Choose only the scenarios and dependencies relevant to
the module; the checker tests are examples, not a required template.

## Plan the next suite

Before writing mocks, identify the module entry points, their observable contract,
and the dependencies they call. List one success path, each meaningful failure
boundary, and the state/resources each path must leave behind. Pick the smallest
test target that compiles the production source and the mocks needed for those
boundaries. Write assertions for the contract, not a copy of the implementation.

Choose the boundary of the suite:

- Give distinct behaviors their own suites when fixtures or dependencies would
  otherwise obscure the tests. `dlck_checker_main_ut.c` and
  `dlck_checker_worker_ut.c` compile the same implementation into separate binaries
  with a shared `dlck_checker_ut_mock.c`.
- Test header-only behavior without initializing unrelated production objects.
  `dlck_checker_macros_ut.c` uses a small `ck_vprintf` callback to inspect messages
  from macros in `daos_srv/checker.h`. Keep one integration case in a module suite
  when the interaction matters: the worker suite checks that `CK_PRINTF` includes
  its configured indentation in the formatted output.
- Keep parser or other module tests close to their owning code. `dlck_args_ut.c`
  exercises parser behavior and wraps only `argp_failure`.

## Turn each path into a case

1. Start with a success case and assert the result and observable initialized state.
   Cover finalization and resource ownership where the module manages resources.
2. Add failure cases at meaningful boundaries: allocation, I/O, external APIs,
   callbacks, and optional reporting. Assert both the returned error and cleanup;
   distinguish `daos_errno2der()` from `dss_abterr2der()` where applicable.
3. Check boundaries and invalid state only where the API promises them. For DAOS
   assertions, register `d_register_alt_assert(mock_assert)` in the test binary
   before using `expect_assert_failure()`.
4. Check the actual output when formatting is the behavior under test. A correct
   `ck_prefix` alone does not prove that `CK_PRINTF` emitted the prefixed message.
   A wrapper can compare the original format and the formatted `va_list` contents.
5. Test the absent-checker (`NULL`) path where the module permits optional reporting.
   Do not add cases only to chase branches introduced by expanded DAOS macros.

For each case, arrange the inputs and CMocka expectations, call the production
entry point once, then assert its return, outputs, state, and resource ownership.
Queue only values that the path will consume; extra `will_return()` values or
unchecked expectations fail the test. Use `setup`/`teardown` for common fixtures
and reset mock flags before each case. Register cases in a
`CMUnitTest` array with `cmocka_run_group_tests_name()`. Keep descriptive IDs
sequential in execution order if the suite uses them; `cmocka_unit_test()` without
numbered IDs is also valid.

## Design wrappers deliberately

1. Wrap a dependency only when the test must control its result or inspect its
  arguments. Add `-Wl,--wrap=<symbol>` to that suite's linker flags, implement
  `__wrap_<symbol>` with the real signature, and declare `__real_<symbol>` if
  the wrapper delegates. Keep the production source in the same test target;
  linker wrapping does not replace calls already compiled into another binary.
2. Model a return with `will_return(__wrap_<symbol>, value)` in the test and
  `mock_type(type)` in the wrapper. For pointer-returning mocks, use
  `mock_ptr_type(type)` and queue the exact pointer (or `NULL`) from the test.
  Queue one result per wrapper invocation, including initialization and
  cleanup calls. Use distinct returns for success and failure paths, and set
  `errno` in an I/O wrapper when the code under test reads it. Do not use
  global flags instead of CMocka queues unless a wrapper must switch between
  real and mocked behavior.
3. Check an argument with `expect_value(__wrap_<symbol>, parameter, expected)`
  and `check_expected_ptr(parameter)` in the wrapper for pointer-like values.
  For strings, use `expect_string()` and `check_expected()`; for numeric values,
  use `expect_value()` and `check_expected()`. The names must match the wrapper
  function and parameter. `expect_ptr()` is not available in this CMocka
  version. Use `assert_ptr_equal()` on resulting state, not as a substitute
  for checking a call's input.

     For example, a wrapper consumes exactly the expectations queued for it:

     ```c
     int
     __wrap_ABT_mutex_create(ABT_mutex *newmutex)
     {
       int rc = mock_type(int);

       check_expected_ptr(newmutex);
       if (rc == ABT_SUCCESS)
         *newmutex = Mock_mutex_handle;
       return rc;
     }
     ```

     The test queues `expect_value(__wrap_ABT_mutex_create, newmutex,
     &Dcm.stream_mutex)` and `will_return(__wrap_ABT_mutex_create, ABT_SUCCESS)`
     before invoking the code under test. Queue an error instead to exercise
    creation failure; assert the mapped error and unchanged checker state.
  4. Verify cleanup at the call boundary. Have the tracked-payload free wrapper
     call `check_expected_ptr(ptr)`, then queue the expected pointer before the
     operation that should free it. This checks the actual free argument and
     causes an unexpected or missing free to fail the case:

  ```c
  expect_checker_d_calloc(sizeof(Dcm), &Dcm);
  expect_value(__wrap_ABT_mutex_create, newmutex, &Dcm.stream_mutex);
  will_return(__wrap_ABT_mutex_create, ABT_SUCCESS);
  assert_int_equal(dlck_checker_main_init(ck), DER_SUCCESS);
  assert_ptr_equal(ck->ck_private, &Dcm);

  expect_value(__wrap_ABT_mutex_free, mutex, &Dcm.stream_mutex);
  will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
  expect_value(__wrap_d_free, ptr, &Dcm);
  assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
  assert_null(ck->ck_private);
  ```

  `expect_checker_d_calloc()` is a shared test helper: it expects `nmemb == 1`
  and the requested size, then queues the pointer returned by the calloc
  wrapper. Pass `NULL` to test allocation failure. Queue the free expectation
  for failure paths that release an allocated payload as well. If the
  operation should not free a payload, do not queue an expectation; an
  unexpected tracked-payload free then fails in the wrapper. This verifies
  the free call and pointer, not general leak freedom.
5. For pass-through wrappers such as `d_calloc`, delegate unrelated calls to
  `__real_d_calloc`; otherwise a broad wrapper can break library setup. Share
  wrappers between suites only if they have the same contract, as in
  `dlck_checker_ut_mock.c`. Keep special fake-resource modes opt-in and reset
  them for each test.

  The checker calloc wrapper is armed explicitly by `expect_checker_d_calloc()`
  through `mock_d_calloc_enabled`. It intercepts exactly the next `d_calloc`
  call, checks the queued arguments, and returns the queued payload; all other
  calls go to `__real_d_calloc` regardless of allocation size. The fopen fake
  stream uses the same one-shot pattern through `mock_fopen_fake_stream_enable`.
  Reset these flags in suite setup, and queue the wrapper return expected by
  each call even when the fake stream is selected.

The shared checker mock flags control these behaviors:

- `mock_d_calloc_enabled` arms `expect_checker_d_calloc()` for one allocation.
- `mock_fopen_fake_stream_enable` makes the next `fopen` return the sentinel
  stream; `__wrap_fclose` recognizes that sentinel.
- `mock_vfprintf_enabled` makes the next `vfprintf` consume its queued result.
- `mock_vfprintf_check_args` additionally checks the expected format string and
  formatted arguments during that mocked `vfprintf` call.
- `mock_fflush_enabled` makes the next `fflush` consume its queued result.

The one-shot flags clear when their corresponding wrapper path runs. Reset all
flags in each suite's setup as well, so a failed or interrupted case cannot
affect the next test.

For error injection, the checker mocks demonstrate `fopen` setting `errno`,
`vfprintf`/`fflush` controlling print failures, and ABT mutex functions returning
Argobots errors. Check both the mapped result and the resulting cleanup state.
When checking a forwarded `va_list`, use `va_copy()` before formatting and
`va_end()` on the copy; check the original format and the formatted output.
Use real filesystem I/O only when file creation or contents are the subject of
the test. A fake `FILE *` must never reach real `vfprintf`, `fflush`, or `fclose`.

## Register a new test binary

In `SConscript`, add a build function for the suite and invoke it with `env.Clone()`.
Use a unique `OBJPREFIX` and `d_test_program()` with only the necessary sources,
libraries, `require()` dependencies, include paths, and linker wraps. The macro
suite is a minimal example; main and worker show how to link a shared mock and the
implementation under test. Then add the executable to `utils/utest.yaml` under
`dlck`, and add its build artifact to `ci/test_files_to_stash.txt` so CI can run it.

## Build and verify

From the DAOS repository root, for the debug/gcc configuration used by the current
checker suites (adjust the build path for other configurations):

```sh
. ~/.venvs/daos/bin/activate
./utils/build/build_daos.sh --build-deps=no BUILD_TYPE=debug \
  build/debug/gcc/src/utils/dlck/tests/<binary>
build/debug/gcc/src/utils/dlck/tests/<binary>
git diff --check
```

Run each affected suite after a change; run both main and worker if their shared
mock changes. For coverage, use a freshly instrumented binary and its matching
`.gcno`/`.gcda` files. Normal debug binaries and old gcov snapshots cannot establish
current coverage; assess uncovered module behavior rather than macro-internal
branches alone.