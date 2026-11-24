# Unit Test Instructions

## General Guidance

- Use `git commit --signoff` for every commit, including amended commits; each requires a
  `Signed-off-by` trailer.
- Follow `.clang-format` for C/C++ and test tables. The column limit is 100; count tabs as 8.

## Unit Tests

- Use the CMocka framework.
- In each CMocka test binary's `main(int argc, char **argv)`, call `dlck_ut_filter(argc, argv)`
  from `dlck_ut.h` only when `argc > 1`. It applies a wildcard filter from the single test-name
  argument and exits on invalid usage or when filtering is unsupported. With no argument, run the
  full suite without calling the helper.
- Don't reproduce the production algorithm in assertions.
- Choose the smallest suitable suite and dependency boundary.
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

- Shared per-area files provide memory, stdio, ABT, DAOS and checker callback mocks. Each test
  binary compiles only the mock areas it needs and enables only its required linker wraps.
- Tests include the headers for the mock areas they use directly; do not rely on
  `ut_dlck_checker_mock.h` to include the required header for you.
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
- For `d_asprintf2()`, use `EXPECT_D_ASPRINTF2(fmt, ptr, rc)` to queue the returned string and
  output status independently.
- For APIs returning status and an output handle, queue the handle only on success.
- For mocks which are expected to set `errno` on error, queue an `errno` only on error.
- Successful free APIs have to have fixed output effects, such as clearing a handle.
- Use a distinct sentinel for each resource instance so tests catch resources mixed between
  execution streams. Initialize ABT handles to their explicit null constants, which may be nonzero.

## Checking Parameters

- CMocka tests check parameters received by mocks by expressing expectations. The mock has to call
  a dedicated `check_*` variant.
- For values: `expect_value()` with `check_expected()`.
- For strings: `expect_string()` with `check_expected()`.
- For pointers: `expect_value()` with `check_expected_ptr()`. This CMocka version does not provide
  `expect_ptr()`.
- Define helpers that register CMocka `expect_*()` expectations as macros, not functions. CMocka
  records the expectation's source location, so a function helper makes unconsumed-expectation
  diagnostics point to the helper instead of the test call site.
- Define an expectation macro only when it queues multiple coupled values or conditionally queues
  values. Place expectation macros next to the mocks they feed.
- Write single expectations and return values inline.

## Strict Mocks

- Always use strict mocks only.
- Especially: `__wrap_d_calloc()` and `__wrap_d_free()` if required by the test.
- For I/O mocks check also printf-style formats.
- Never use `__real_*()` variants.
- Never pass fake `FILE *` values to real stdio.

## DLCK Engine Batch

- Exercise batch initialization, execution, joining and finalization separately in
  `dlck_engine_ut`. Test `dlck_engine_exec_all()` orchestration in `dlck_engine_exec_all_ut`,
  mocking every function declared in `dlck_engine_internal.h`. The start message spans
  initialization and execution; joining reports its own result.
- The caller owns and initializes `dlck_exec`, including its ULT callback, allocation callbacks,
  target count and optional checker. `dlck_exec_fini()` releases resources, not the structure.
- After execution or joining, propagate the batch's `join_fail` to the engine before finalization.
  Finalization must not free arguments belonging to ULTs that have not joined.
- Test allocation and partial-start failures, join failures, and cleanup with distinct resources
  for each target. Allocation cleanup may call the free callback with a null argument.
- The `dlck_engine_ut` binary replaces `dlck_abt.c` with the `ut_dlck_abt_mock.c` mocks. Its
  `dlck_ult_create` mock runs successful ULTs synchronously, so queue their expectations before
  creating them. Use per-stream sentinels for retained handles; transient ULTs can share
  `Mock_thread`.

## DLCK Checker Tests

- Keep each checker suite at the smallest useful boundary. `dlck_checker_common_ut.c` tests
  `ck_common_printf()` dispatch through `mock_ck_vprintf`; it should not initialize main or worker
  state. `dlck_checker_main_ut.c` and `dlck_checker_worker_ut.c` compile the respective checker
  implementations into separate binaries with only the required payloads and per-area mocks.
- For payload allocation, use `EXPECT_D_CALLOC(1, sizeof(payload), &payload)` on success and
  `EXPECT_D_CALLOC(1, sizeof(payload), NULL)` on failure.
- `mock_ck_vprintf` always checks the format string. Use the `CHECK_OUTPUT` and `OUTPUT` arguments
  of `EXPECT_MOCK_CK_VPRINTF` to optionally check the rendered callback output.
- The stdio `vfprintf` mock always checks the format string. Use `EXPECT_VFPRINTF` to queue
  its stream, format, `CHECK_OUTPUT` flag, `OUTPUT` string, and return value; provide `errno` on
  failure. Set `OUTPUT` to `NULL` when `CHECK_OUTPUT` is `false`.
- From the DAOS root, activate the Python environment configured for your DAOS development setup,
  then build and run the affected dev/gcc test binary:

  ```sh
  ./utils/build/build_daos.sh --build-deps=no BUILD_TYPE=dev
  ./build/dev/gcc/src/utils/dlck/tests/<binary>
  ```

- Pass a fragment of a test name as the first argument to run matching tests.
