## Summary

This version brings bug fixes and updates to our v2.0.0 release.

## New features

- __[HG/NA]__
    - Add `HG_Init_opt2()` / `HG_Core_init_opt2()` / `NA_Initialize_opt2()` to
    safely pass updated init info while maintaining ABI compatibility between
    versions
    - Add `HG_Get_na_protocol_info()` / `HG_Free_na_protocol_info()` and add
    `hg_info` utility for basic listing of protocols
- __[HG]__
    - Add support for multi-recv operations (OFI plugin only)
        - Currently disable multi-recv when auto SM is on
        - Posted recv operations are in that case decoupled from pool of RPC
        handles
        - Add `release_input_early` init info flag to attempt to release input
        buffers early once input is decoded
        - Add `HG_Release_input_buf()` to manually release input buffer.
        - Add also `no_multi_recv` init info option to force disabling
        multi-recv
    - Make use of subsys logs (`cls`, `ctx`, `addr`, `rpc`, `poll`) to control
    log output
    - Add init info struct versioning
    - Add `HG_Context_unpost()` / `HG_Core_context_unpost()` for optional
    2-step context shutdown
- __[HG bulk]__
    - Update to new logging system through `bulk` subsys log.
- __[HG proc]__
    - Update to new logging system through `proc` subsys log.
- __[HG Test]__
    - Refactor tests to separate perf tests from unit tests
    - Add NA/HG test common library
    - Add `hg_rate` / `hg_bw_write` and `hg_bw_read` perf tests
        - Perf test now supports multi-client / multi-server workloads
    - Add `BUILD_TESTING_UNIT` and `BUILD_TESTING_PERF` CMake options
- __[NA]__
    - Add support for multi-recv operations
        - Add `NA_Msg_multi_recv_unexpected()` and
        `na_cb_info_multi_recv_unexpected` cb info
        - Add `flags` parameter to `NA_Op_create()` and `NA_Msg_buf_alloc()`
        - Add `NA_Has_opt_feature()` to query multi recv capability
    - Remove `int` return type from NA callbacks and return `void`
    - Remove unused `timeout` parameter from `NA_Trigger()`
    - `NA_Addr_free()` / `NA_Mem_handle_free()` and `NA_Op_destroy()` now
    return `void`
    - `na_mem_handle_t` and `na_addr_t` types no longer include pointer type
    - Add support for dynamically loaded plugins
        - Add `NA_PLUGIN_PATH` env variable to optionally control plugin loading
        path (default is `NA_INSTALL_PLUGIN_DIR`)
        - Add `NA_INSTALL_PLUGIN_DIR` variable to control plugin install path
        (default is lib install path)
        - Add `NA_USE_DYNAMIC_PLUGINS` CMake option (OFF by default)
    - Add ability to query protocol info from plugins
        - Add `NA_Get_protocol_info()`/`NA_Free_protocol_info()` API routines
        - Add `na_protocol_info` struct to na_types
    - Bump NA library version to 4.0.0
- __[NA OFI]__
    - Add support for multi-recv operations and use `FI_MSG`
    - Allocate multi-recv buffers using hugepages when available
    - Switch to using `fi_senddata()` with immediate data for unexpected msgs
        - `NA_OFI_UNEXPECTED_TAG_MSG` can be set to switch back to former
        behavior that uses tagged messages instead
    - Remove support for deprecated `psm` provider
    - Control CQ interrupt signaling with `FI_AFFINITY` (only used if thread is
    bound to a single CPU ID)
    - Enable `cxi` provider to use `FI_WAIT_FD`
    - Add `NA_OFI_OP_RETRY_TIMEOUT` and `NA_OFI_OP_RETRY_PERIOD`
        - Once `NA_OFI_OP_RETRY_TIMEOUT` milliseconds elapse, retry is stopped
        and operation is aborted (default is 120000ms)
        - When `NA_OFI_OP_RETRY_PERIOD` is set, operations are retried only
        every `NA_OFI_OP_RETRY_PERIOD` milliseconds (default is 0)
    - Add support for `tcp` with and without `ofi_rxm`
        - `tcp` defaults to `tcp;ofi_rxm` for libfabric < 1.18
    - Enable plugin to be built as a dynamic plugin
    - Add support for `get_protocol_info` to query list of protocols
    - Add support for libfabric log redirection
        - Requires libfabric >= 1.16.0, disabled if FI_LOG_LEVEL is set
        - Add `libfabric` log subsys (off by default)
        - Bump FI_VERSION to 1.13 when log redirection is supported
- __[NA UCX]__
    - Attempt to disable UCX backtrace if `UCX_HANDLE_ERRORS` is not set
    - Add support for `UCP_EP_PARAM_FIELD_LOCAL_SOCK_ADDR`
        - With UCX >= 1.13 local src address information can now be specified
        on client to use specific interface and port
    - Set `CM_REUSEADDR` by default to enable reuse of existing listener addr
    after a listener exits abnormally
    - Attempt to reconnect EP if disconnected
        - This concerns cases where a peer would have reappeared after a
        previous disconnection
    - Add support for `get_protocol_info`  to query list of protocols
    - Enable plugin to be built as a dynamic plugin
- __[NA Test]__
    - Update NA test perf to use multi-recv feature
    - Update perf test to use hugepages
    - Add support for multi-targets and add lookup test
    - Install perf tests if `BUILD_TESTING_PERF` is `ON`
- __[HG util]__
    - Change return type of `hg_time_less()` to `bool`
    - Add `HG_LOG_WRITE_FUNC()` macro to pass func/line info
        - Add also `module` / `no_return` parameters to hg_log_write()
    - Add support for hugepage allocations
    - Use `isb` for `cpu_spinwait` on `aarch64`
    - Add `mercury_dl` to support dynamically loaded modules
    - Bump HG util version to 4.0.0

## Bug fixes

- __[HG]__
    - Ensure init info version is compatible with previous versions of the struct
    - Clean up and refactoring fixes
    - Fix race condition in `hg_core_forward` with debug enabled
    - Simplify RPC map and fix hashing for RPC IDs larger than 32-bit integer
    - Refactor context pools and cleanup
    - Fix potential leak on ack buffer
    - Ensure list of created RPC handles is empty before closing context
    - Bump default number of pre-allocated requests from 256 to 512 to make use
    of 2M hugepages by default
    - Add extra error checking to prevent class mismatch
    - Fix potential race when sending one-way RPCs to ourself
- __[HG Bulk]__
    - Add extra error checking to prevent class mismatch
- __[HG Test]__
    - Refactor `test_rpc` to correctly handle timeout return values
    - Fix overflow of number of target / classes
        - Number of targets was limited to `UINT8_MAX`
- __[NA OFI]__
    - Fix handling of extra caps to not always follow advertised caps
        - Ensure also that extra caps passed are honored by provider
    - Force `sockets` provider to use shared domains
        - This prevents a performance regression when multiple classes are
        being used (`FI_THREAD_DOMAIN` is therefore disabled for this provider)
    - Refactor unexpected and expected sends, retry of OFI operations, handling
    of RMA operations
    - Always include `FI_DIRECTED_RECV` in primary caps
    - Disable use of `FI_SOURCE` for most providers to reduce lookup overhead 
        - Separate code paths for providers that do not support `FI_SOURCE`
        - Remove insert of FI addr into secondary table if `FI_SOURCE` is
        not used
    - Remove `NA_OFI_SOURCE_MSG` flag that was matching `FI_SOURCE_ERR`
    - Fix potential refcount race when sharing domains
    - Check domain's optimal MR count if non-zero
    - Fix potential double free of src_addr info
    - Refactor auth key parsing code to build without extension headers
    - Merge latest changes required for `opx` provider enablement
        - Pass `FI_COMPLETION` to RMA ops as flag is currently not ignored
        (`prov/opx` tmp fix)
    - Add runtime version check
        - Ensure that runtime version is greater than min version
- __[NA SM]__
    - Fix handling of 0-size messages when no receive has been posted
    - Fix issue where an expected msg that is no longer posted arrives
        - In that particular case just drop the incoming msg
    - Add perf warning message for unexpected messages without recv posted
- __[NA UCX]__
    - Fix handling of UCS return types to match NA types
    - Enforce src_addr port used for connections to be 0
        - This fixes a port conflict between listener and connection ports
    - Fix handling of unexpected messages without pre-posted recv
- __[NA BMI]__
    - Clean up and fix some coverity warnings
- __[NA MPI]__
    - Clean up and fix some coverity warnings
- __[NA Test]__
    - Fix NA latency test to ensure recvs are always pre-posted
    - Do not use MPI_Init_thread() if not needed
        - Fix missing return check of na_test_mpi_init()
- __[HG util]__
    - Clean up logging and set log root to `hg_all`
        - `hg_all` subsys can now be set to turn on logging in all subsystems
    - Set log subsys to `hg_all` if log level env is set
    - Fixes to support WIN32 builds
- __[CMake]__
    - Fix internal/external dependencies that were not correctly set
    - Fix pkg-config entries wrongly set as public/private
    - Ensure `VERSION`/`SOVERSION` is not set on `MODULE` libraries
    - Allow for in-source builds (RPM support)
    - Add `DL` lib dependency
    - Fix object target linking on CMake < 3.12
    - Ensure we build with PIC and PIE when available
- __[Examples]__
    - Allow examples to build without Boost support

## :warning: Known Issues

- __[NA OFI]__
    - [tcp/verbs;ofi_rxm] Using more than 256 peers requires `FI_UNIVERSE_SIZE`
    to be set.
