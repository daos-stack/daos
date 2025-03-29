## Summary

This version brings bug fixes and updates to our v2.3.0 release.

## New features

- __[HG info]__
    - Add support for CSV and JSON output formats
- __[HG/NA Perf Test]__
    - Enable sizes to be passed using k/m/g qualifiers
- __[NA OFI]__
    - Add `tcp_rxm` alias for `tcp;ofi_rxm`
    - Find CXI `svc_id` or `vni` if `auth_key` components have zeros (e.g., `auth_key=0:0`)
        - Add VNI index for `SLINGSHOT_VNIS` discovery as extra auth_key parameter

## Bug fixes

- __[HG/NA]__
    - Fix potential race when checking secondary completion queue
- __[HG]__
    - Prevent multiple threads from entering `HG_Core_progress()`
        - Add `HG_ALLOW_MULTI_PROGRESS` CMake option to control behavior (`ON` by default)
        - Disable `NA_HAS_MULTI_PROGRESS` if `HG_ALLOW_MULTI_PROGRESS` is `ON`
    - Fix expected operation count for handle to be atomic
        - Expected operation count can change if extra RPC payload must be transferred
    - Let poll events remain private to HG poll wait
        - Prevent a race when multiple threads call progress and `HG_ALLOW_MULTI_PROGRESS` is `OFF`
    - Separate internal list from user created list of handles
        - Address an issue where `HG_Context_unpost()` would unnecessarily wait
- __[HG Core]__
    - Cache disabled response info in proc info
    - Add `HG_Core_registered_disable(d)_response()` routines
    - Refactor and optimize self RPC code path
    - Add additional logging of refcount/expected op count
    - Fixes for self RPCs with no response
- __[HG Util]__
    - Prevent locking in `hg_request_wait()`
        - Concurrent progress in multi-threaded scenarios on the same context could complete another thread's request and let a thread blocked in progress
- __[HG Perf]__
    - Fix tests to be run in parallel with any communicator size
- __[HG Test]__
    - Ensure affinity of class thread is set
    - Add concurrent multi RPC test
    - Add multi-progress test
    - Add multi-progress test with handle creation
    - Refactoring of unit test cleanup
- __[NA]__
    - Fix memory leak on `NA_Get_protocol_info()`
- __[NA OFI]__
    - Fix `na_ofi_get_protocol_info()` not returning `opx` protocol
        - Refactor `na_ofi_getinfo()` to account for `NA_OFI_PROV_NULL` type
        - Ensure there are no duplicated entries
    - Refactor parsing of init info strings and fix OPX parsing
    - Simplify parsing of some address strings
    - Bump default CQ size to have a maximum depth of 128k entries
    - Remove sockets as the only provider on macOS
    - Remove send afer send tagged msg ordering
    - Ensure that `rx_ctx_bits` are not set if SEP is not used
    - Set CXI domain ops w/ slingshot 2.2 to prevent from potential memory corruptions
- __[NA Perf]__
    - Prevent tests from being run as parallel tests
- __[CMake]__
    - Pass `INSTALL_NAME_DIR` through target properties
        - This fixes an issue seen on macOS where libraries would not be found using `@rpath`

## :warning: Known Issues

- __[NA OFI]__
    - [tcp/verbs;ofi_rxm] Using more than 256 peers requires `FI_UNIVERSE_SIZE` to be set.
