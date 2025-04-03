## Summary

This version brings bug fixes and updates to our v2.0.0 release.

## New features

- __[NA UCX]__
    - Add initial support for UCX. As opposed to other plugins, the UCX plugin is able through the `ucx+all` init string to decide on which protocol to use.
- __[NA SM]__
    - Update default addressing format to follow `PID-ID` instead of `PID/ID`
    - Allow for passing of arbitrary SM init URIs
    - Enable support for bulk handle address binding
    - Add `sm_info_string` field to HG init info, which allows for specific init URIs to be used for SM when `auto_sm` is enabled.
- __[NA]__
    - Add `thread_mode` to NA init options and add `NA_THREAD_MODE_SINGLE` to relax thread-safety requirements.
    - Add `na_cb_info_recv_expected` to return `actual_buf_size`.
    - Add `na_cb_type_to_string()` to convert callback enum type to printable string.
- __[NA IP]__
    - Add `na_ip_check_interface()` routine that can be used by plugins to select IP interface to use.
- __[HG util]__
    - Add `hg_mem_header_alloc()`/`free()` calls to allocate buffers with a preceding header.
    - Add thread annotation module for thread safety analysis.
    - Add `mercury_mem_pool` memory pool to facilitate allocation and memory registration of a pool of buffers.
    - Enable format argument checking on logging functions.
    - Add `hg_time_from_ms()` and `hg_time_to_ms()` for time conversion to ms.
- __[HG bulk]__
    - Return transfer size `size` through `hg_cb_info` and `hg_cb_info_bulk`.

## Bug fixes

- __[NA OFI]__
    - Require at least v1.7.0 of libfabric.
    - Fix handling of completion queue events and completion of retried operations that fail.
    - Fix progress loop to reduce time calls.
    - Force per-region registration for all providers and remove deprecated FI_MR_SCALABLE type of registrations and global MR keys.
- __[NA SM]__
    - Refactoring and clean up of sends/cancelation/retries/rma/address keys.
    - Remove use of usernames from SM paths.
- __[HG util]__
    - Prevent use of `CLOCK_MONOTONIC_COARSE` on PPC platforms and default to `CLOCK_MONOTONIC`.
    - Fix debug logs that were not freed at exit.
    - Remove return value of mutex lock/unlock routines.
    - Fix log subsys to prevent setting duplicates.
    - Simplify handling of compiler attributes and add `mercury_compiler_attributes.h` module.
    - Remove `hg_util_` integer types and use `stdint.h`.
    - Remove OpenPA dependency for atomics and use built-in atomics instead (requires gcc >= 4.7).
- __[HG/HG util/NA]__
    - Fix thread safety warnings and potential thread locking issues.
    - Fix log level set routines that were not enabling the underlying log sub-system.
    - Avoid reading system timers and optimize handling of timeouts. 
- __[HG bulk]__
    - Fix erroneous call to `NA_Mem_deregister()` when handle is deserialized.
    - Correctly mark op as canceled if canceled from NA.
    - Clean up and simplify handling of NA error return codes in callback.
    - Minimal tracking of bulk handles that are not freed.
- __[HG Core]__
    - Fix error handling when NA send fails during an `HG_Forward()` operation.
    - Correctly map NA error return code back to HG error return code in user callback.
    - Correctly print HG handle debug information.
    - In short responses like ACKs, leave room at the front of a buffer for
    the NA header, and expect the header to be present.
    - Fix potential issue on context destroy where handles could have been reposted while finalizing if RPCs were still in the queue.
- __[General]__
    - Warning and static analysis issues were fixed.


## :warning: Known Issues

- __[NA OFI]__
    - [tcp/verbs;ofi_rxm] Using more than 256 peers requires `FI_UNIVERSE_SIZE` to be set.
    - [tcp;ofi_rxm] Remains unstable, use `sockets` as a fallback in case of issues.
        - __Please note that libfabric v1.13.0 and v1.13.1 have address management issues with that transport. Please either downgrade to v1.12.1 (or earlier) or upgrade to v1.13.2 (or later).__
- __[NA UCX]__
    - `NA_Addr_to_string()` cannot be used on non-listening processes to convert a self-address to a string.
