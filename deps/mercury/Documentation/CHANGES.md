## Summary

This new version brings both bug fixes and feature updates to mercury. Notable
are the addition of a new progress mechanism, new initialization parameters for
the handling of multi-recv buffers and the support of cxi with HPE SHS 11.0.

## New features

- __[HG]__
    - Add `HG_Get_input_payload_size()`/`HG_Get_output_payload_size()`
        - Add the ability to query input / output payload sizes
    - Add `HG_Diag_dump_counters()` to dump diagnostic counters
        - Add `rpc_req_recv_active_count` and `rpc_multi_recv_copy_count` counters
    - Add `HG_Class_get_counters()` to retrieve internal counters
    - Add `multi_recv_copy_threshold` init parameter
        - Use this new parameter to fallback to memcpy to prevent starvation of multi-recv buffers
    - Add `multi_recv_op_max` init parameter
        - This allows users to control number of multi-recv buffers posted (libfabric plugin only)
    - Add `no_overflow` init option to prevent use of overflow buffers
    - Improve multi-recv buffer warning messages
    - Associate handle to HG proc
        - `hg_proc_get_handle()` can be used to retrieve handle within proc functions
    - Add `HG_Event_get_wait_fd()` to retrieve internal wait object
    - Add `HG_Event_ready()` / `HG_Event_progress()` / `HG_Event_trigger()` to support wait fd progress model
        - Simplify progress mechanism and remove use of internal timers
        - Always make NA progress when `HG_Event_progress()` is called
        - Update HG progress to use new NA progress routines
    - Add missing `HG_WARN_UNUSED_RESULT` to HG calls
    - Switch to using standard types and align with NA
        - Keep some `uint8_t` instances instead of `hg_bool_t` for ABI compatibility
    - Add `HG_IO_ERROR` return code
- __[NA]__
    - Bump NA version to v5.0.0
    - Add `NA_Poll()` and `NA_Poll_wait()` routines
    - Deprecate `NA_Progress()` in favor of poll routines
    - Add `NA_Context_get_completion_count()` to retrieve size of completion queue
    - Update plugins to use new `poll` and `poll_wait` callbacks
        - `poll_wait` plugin callback remains for compatibility
    - Fix documentation of `NA_Poll_get_fd()`
    - Add missing `NA_WARN_UNUSED_RESULT` qualifiers
    - Remove deprecated CCI plugin
    - Return last known error when plugin loading fails
    - Add init info version compatibility wrappers
    - Add support for `traffic_class` init info (only supported by ofi plugin)
    - Add `NA_IO_ERROR` return code for generic I/O errors
        - Update OFI and UCX plugins to use new code
- __[NA OFI]__
    - Support use of cxi provider with SHS 11.0
    - Add support for `FI_AV_AUTH_KEY` (requires libfabric >= 1.20)
        - Add runtime check for cxi provider version
        - Setting multiple auth keys disables `FI_DIRECTED_RECV`
        - Separate opening of AV and auth key insertion
        - Parse auth key range when `FI_AV_AUTH_KEY` is available
        - Encode/decode auth key when serializing addrs
    - Add support for `FI_AV_USER_ID`
    - Always use `FI_SOURCE` and `FI_SOURCE_ERR` when both are supported
        - Clean up handling of `FI_SOURCE_ERR`
        - Remove support of `FI_SOURCE` w/o `FI_SOURCE_ERR`
    - Add support for new CXI address format
    - Attempt to distribute multi-NIC domains based on selected CPU ID
    - Support selection of traffic classes (single class per NA class)
    - Add support for `FI_PROTO_CXI_RNR`
    - Add `NA_OFI_SKIP_DOMAIN_OPS` env variable to skip cxi domain ops
    - Remove unused `NA_OFI_DOM_SHARED` flag
- __[NA UCX]__
    - Add `ucx` log outlet and redirect UCX log
        - Use default HG log level if `UCX_LOG_LEVEL` is not set
- __[HG/NA perf]__
    - Add `hg_first` perf test to measure cost of initial RPC
    - Add `-u` option to control number of multi-recv ops (server only)
    - Add `-i` option to control number of handles posted (server only)
    - Add `-f`/`--hostfile` option to select hostfile to write to / read from
    - Add `-T`/`--tclass` option to select trafic class
    - Autodetect MPI implementation in perf utilities
        - MPI can now be autodetected and dynamically loaded in utilities, even if `MERCURY_TESTING_ENABLE_PARALLEL` was turned off. If `MERCURY_TESTING_ENABLE_PARALLEL` is turned on, tests remain manually linked against MPI as they used to be.
    - Print registration and deregistration times when `-R` option is used
    - Update to use new HG/NA progress routines and remove use of `hg_request`
    - Support forced registration in `hg_bw_read`/`hg_bw_write`
- __[HG Util]__
    - Add `hg_log_vwrite()` to write log from `va_list`
    - Add `hg_log_level_to_string()`
    - Clean up `mercury_event` code and add `const` qualifier to `hg_poll_get_fd()`
    - Add `const` on atomic gets
    - Switch to using `sys/queue.h` directly
    - Remove `HG_QUEUE` and `HG_LIST` definitions
    - Add `hg_dl_error()` to return last error

## Bug fixes

- __[HG]__
    - Fix shared-memory path that was previously disabled in conjunction with libfabric transports that use the multi-recv capability
    - Fix behavior of `request_post_incr` init parameter
        - `request_post_incr` cannot be disabled (set to -1) with multi-recv
- __[HG/NA]__
    - HG NA init info is fixed to v4.0 for now and duplicates tclass info
- __[NA]__
    - Fix missing free of dynamic plugin entries
- __[NA BMI/MPI]__
    - Return actual msg size through cb info
- __[NA OFI]__
    - Fix cxi domain ops settings and disable `PROV_KEY_CACHE`
    - Fix shm provider flags
    - Remove excessive MR count warning message
- __[NA UCX]__
    - Fix `hg_info` not filtering protocol   
        - Allow `na_ucx_get_protocol_info()` to resolve ucx tl name aliases
    - Fix context thread mode to default to `UCS_THREAD_MODE_MULTI`
- __[HG/NA Perf]__
    - Ensure NA perf tests wait on send completion
    - Fix bulk permission flag in `hg_bw_read`
    - Add some missing error checks in mercury_perf
- __[HG util]__
    - Multiple logging fixes:
        - Fix `dlog_free` not called when parent/child have separate dlogs
        - Fix mercury log to correctly generate outlet names
        - Fix log outlets to use prefixed subsys name
        - Fix use of macros in debug log
        - Use destructor to free log outlets
    - Add missing prototype to `hg_atomic_fence()` definition
- __[CMake]__
    - Fix cmake_minimum_required() warning
    - Update kwsys and mchecksum dependencies

## :warning: Known Issues

- __[NA OFI]__
    - [tcp/verbs;ofi_rxm] Using more than 256 peers requires `FI_UNIVERSE_SIZE` to be set.
