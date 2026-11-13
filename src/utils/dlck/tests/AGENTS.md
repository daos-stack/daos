# Unit Test Instructions

## General Guidance

- Use `git commit --signoff` for every commit, including amended commits; each requires a
  `Signed-off-by` trailer.
- Follow `.clang-format` for C/C++ and test tables. The column limit is 100; count tabs as 8.
- Declare local variables at the top of the function. Keep a single empty line after the
  declarations and before the final `return`; avoid empty lines between logically coupled
  statements.
- Describe behavior in comments in plain words; do not quote code or function signatures.

## Unit Tests

- Use the CMocka framework.
- Always add command-line CMocka filtering in each test binary's `main(int argc, char **argv)`
  by calling the shared `dlck_ut_filter()` helper from `dlck_ut.h` and returning its non-zero
  status on error. With no argument, the full suite runs.
- Don't reproduce the production algorithm in assertions.
- Choose the smallest suitable suite and dependency boundary.
- If a function cannot be sensibly unit tested, consider splitting it at the module interface
  instead of writing scenario-sized expectations. If the tested interface turns out to be dead
  code, remove it instead of testing it.
- Prefer test-local objects over file-scope buffers; keep file-scope buffers only for the payloads
  returned by the allocation mock and shared by composite expectation macros.
- When the tested code only stores or forwards a pointer, pass an opaque sentinel instead of
  constructing a real object.
- To use the DAOS fault injections, call `daos_fail_init()` in `main()` first. Trigger a fault
  with `daos_fail_loc_set(<fault id> | DAOS_FAIL_ONCE)` and `daos_fail_value_set()`; reset the
  fault location in the per-test teardown.
- Separate module and header-macro tests.
- Give every test function at least one concise comment line describing the behavior it verifies.
- For suites with repeated CMocka registration fields, define small registration macros for common
  fixture variants (for example, with setup/teardown and without a fixture). Keep IDs sequential and
  derive the test name from the ID and function name so table entries stay concise and consistent.
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
- Build and run affected tests.

## Mocking Functions

- Shared, per-area mock files (`dlck_ut_mem_mock.[ch]`, `dlck_ut_abt_mock.[ch]`,
  `dlck_ut_stdio_mock.[ch]`, `dlck_ut_daos_mock.[ch]`, `dlck_ut_checker_mock.[ch]`) provide the
  mocks for memory, Argobots, stdio, DAOS, and the checker callback respectively. Binaries compile
  only the areas they need and enable only the wraps they use; suite-specific mock files keep only
  their own payloads and helpers.
- CMocka tests control the behavior of mocks by populating their respective queues with values.
  All of these values have to be later consumed by the mocks. CMocka queues are maintained per
  mock.
- Use `will_return(mock, value)` to queue test-controlled values for a mock.
- Mocks consume queued values by calling `mock_type(type)` for values and `mock_ptr_type(type)`
  for pointers.
- Queue only the values consumed along the tested path, including during initialization and
  cleanup; omit unused values.
- Do not hardcode values on the mock side.
- For pointer-returning I/O mocks such as `fopen`, queue a mock-pointer or `NULL`.
- For APIs returning status and an output handle, queue the handle only on success.
- For mocks which are expected to set `errno` on error, queue an `errno` only on error.
- Successful free APIs have to have fixed output effects, such as clearing a handle.
- Use a distinct sentinel value for every distinct resource instance (for example a separate pool,
  eventual, and thread per execution stream) so the tests catch the production code mixing up
  resources; share a sentinel only between uses which can never meet.
- The `ABT_*_NULL` constants may be non-zero; initialize the handles explicitly instead of relying
  on zeroed memory.

## Checking Parameters

- CMocka tests check parameters received by mocks by expressing expectations. The mock has to call
  a dedicated `check_*` variant.
- For values: `expect_value()` with `check_expected()`.
- For strings: `expect_string()` with `check_expected()`.
- For pointers: `expect_value()` with `check_expected_ptr()`. This CMocka version does not provide
  `expect_ptr()`.
- Define helpers that register CMocka `expect_*()` expectations as macros, not functions. CMocka
  records the expectation's source location, so a function helper makes unconsumed-expectation
  diagnostics point to the helper instead of the test call site. Define such a macro only when it
  queues two or more coupled values or contains conditional queueing; write single
  `expect_*()`/`will_return()` calls inline.
- Define such a macro next to the mock it feeds: in the area mock header for a shared mock, in
  the test file for a test-local double or a suite-specific scenario composite.
- Name an expectation macro after the full name of the mocked function (for example
  `EXPECT_DSS_TLS_INIT` for `dss_tls_init()`, not `EXPECT_TLS_INIT`).
- Do not use reserved library prefixes (such as `ABT_`) in macro parameter names; they could
  shadow a library identifier of the same name inside the macro body.

## Strict Mocks

- Always use strict mocks only.
- Especially: `__wrap_d_calloc()` and `__wrap_d_free()` if required by the test.
- For I/O mocks check also printf-style formats.
- Never use `__real_*()` variants.
- Never pass fake `FILE *` values to real stdio.

## DLCK Engine Tests

- `dlck_engine_ut.c` compiles the engine implementation with the per-area mocks; `dlck_abt.c` is
  replaced by the mocks entirely.
- The `dlck_ult_create` mock runs the created ULT synchronously on success, so all the values the
  ULT body consumes have to be queued before the call which creates it.
- Transient ULTs created and freed within a single call site use the shared `Mock_thread`
  sentinel; ULTs whose handles are stored and used later get per-stream sentinels.

## DLCK Checker Tests

- Keep checker suites at the smallest useful boundary:
  - `dlck_checker_common_ut.c` tests `ck_common_printf()` with the shared `mock_ck_vprintf`
    callback. Do not initialize main or worker state for this callback-dispatch contract.
- `dlck_checker_main_ut.c` and `dlck_checker_worker_ut.c` compile the checker implementation
  into separate binaries with the shared checker payloads (`dlck_checker_ut_mock.c`) and the
  per-area mock files they need.
- Use `EXPECT_D_CALLOC(1, sizeof(payload), &payload)` for successful payload allocation;
  use `EXPECT_D_CALLOC(1, sizeof(payload), NULL)` for failure.
- Reset `mock_vfprintf_check_output` and `mock_ck_vprintf_check_output` in per-test setup.
  `__wrap_vfprintf` and `mock_ck_vprintf` always check their formats; set the respective flag to
  also check rendered `va_list` output.
- From the DAOS root, build/run a debug/gcc suite with:

  ```sh
  ./utils/build/build_daos.sh --build-deps=no BUILD_TYPE=debug \
    build/debug/gcc/src/utils/dlck/tests/<binary>
  build/debug/gcc/src/utils/dlck/tests/<binary>
  ```

  - Use Python virtual environment for the `build` step.
