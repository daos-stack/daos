## Summary

This version brings bug fixes and updates to our v2.0.0 release.

## New features

- __[NA OFI]__
    - Choose addr format dynamically based on user preferences
    - Add support for IPv6
    - Add support for `FI_SOCKADDR_IB`
    - Add support for `FI_ADDR_STR` and shm provider
    - Add support for `FI_ADDR_OPX` and opx provider
    - Add support for HPE `cxi` provider,
      init info format for `cxi` is:
        - `NIC:PID` (both or only one may be passed), NIC is `cxi[0-9]`, PID is `[0-510]`
    - Use `hwloc` to select interface to use if NIC information is available
      (only supported by `cxi` at the moment)
    - Support device memory types and `FI_HMEM` for `verbs` and `cxi` providers
    - Add support for `FI_THREAD_DOMAIN`
        - Passing `NA_THREAD_MODE_SINGLE` will relax default `FI_THREAD_SAFE`
        thread mode and use `FI_THREAD_DOMAIN` instead.
    - Update min required version to libfabric 1.9
    - Improve debug output to print verbose FI info of selected provider
- __[NA UCX]__
    - Use active messaging `UCP_FEATURE_AM` for unexpected messages (only), this
      allows for removal of address resolution and retry on first message to
      exchange connection IDs
    - Turn on mempool by default
    - Support device memory types
    - Bump min required version to 1.10
- __[NA PSM]__
    - Add mercury NA plugin for the qlogic/intel PSM interface
        - Also support PSM2 (Intel OmniPath) through the PSM NA plugin
- __[NA SM]__
    - Add support for 0-size messages
- __[NA]__
    - Add `na_addr_format` init info
    - Add `request_mem_device` init info when GPU support is requested
    - Update `NA_Mem_register()` API call to support memory types (e.g., CUDA, ROCm, ZE) and devices IDs
    - Add `na_loc` module for `hwloc` detection
    - Remove `na_uint`, `na_int`, `na_bool_t` and `na_size_t` types
    - Use separate versioning for library and update to v3.0.0
- __[NA IP]__
    - Refactor `na_ip_check_interface()` to only use `getaddrinfo()` and `getifaddrs()`
    - Add family argument to force detection of IPv4/IPv6 addresses
    - Add ip debug log
- __[NA Test]__
    - Introduce new perf tests to measure msg latency, put / get bandwidth. These
    benchmarks produce results that are comparable with OSU benchmarks.
- __[HG util]__
    - Add `mercury_byteswap.h` for `bswap` macros
    - Add `mercury_inet.h` for `htonll` and `ntohll` routine
    - Add `mercury_param.h` to use `sys/param.h` or `MIN/MAX` macros etc
    - Add alternative log names: `err`, `warn`, `trace`, `dbg`
    - Use separate versioning for library and update to v3.0.0
- __[HG bulk]__
    - Add support for memory attributes through a new `HG_Bulk_create_attr()` routine (support CUDA, ROCm, ZE)
- __[HG]__
    - Remove `MERCURY_ENABLE_STATS` CMake option and use `'diag'` log subsys instead
        - Modify behavior of `stats` field to turn on diagnostics
        - Refactor existing counters (used only if debug is on)
    - Add checksum levels that can be manually controlled at runtime (disabled by default, `HG_CHECKSUM_NONE` level)
    - Update to mchecksum v2.0
    - Add `HG_Set_log_func()` and `HG_Set_log_stream()` to control log output

## Bug fixes

- __[NA OFI]__
    - Switch `tcp` provider to `FI_PROGRESS_MANUAL`
    - Prevent empty authorization keys from being passed
    - Check max MR key used when `FI_MR_PROV_KEY` is not set
    - New implementation of address management
        - Fix duplicate addresses on multithreaded lookups
        - Redefine address keys and raw addresses to prevent allocations
        - Use FI addr map to lookup by FI addr
        - Improve serialization and deserialization of addresses
    - Fix provider table and use EP proto
    - Refactor and clean up plugin initialization 
        - Clean up ip and domain checking
        - Ensure interface name is not used as domain name for verbs etc
        - Use NA IP module and add missing `NA_OFI_VERIFY_PROV_DOM` for `tcp` provider
        - Rework handling of `fi_info` to open fabric/domain/endpoint
        - Separate fabric from domain and keep single domain per NA class
        - Refactor handling of scalable vs standard endpoints
    - Improve handling of retries after `FI_EAGAIN` return code
        - Abort retried ops after default 90s timeout
        - Abort ops to a target being retried after first `NA_HOSTUNREACH` error in CQ
- __[NA UCX]__
    - Fix potential error not returned correctly on `conn_insert()`
    - Fix potential double free of worker_addr
    - Remove use of unified mode
    - Ensure address key is correctly reset
    - Fix hostname / net device parsing to allow for multiple net devices
- __[HG util]__
    - Make sure we round up ms time conversion, this ensures that small timeouts
    do not result in busy spin.
    - Use sched_yield() instead of deprecated pthread_yield()
    - Fix `'none'` log level not recognized
    - Fix external logging facility
    - Let mercury log print counters on exit when debug outlet is on
- __[HG proc]__
    - Prevent call to `save_ptr()/restore_ptr()` during `HG_FREE`
- __[HG Bulk]__
    - Remove some `NA_CANCELED` event warnings.
- __[HG]__
    - Properly handle error when overflow bulk transfer is interrupted. Previously the RPC callback was triggered regarldless, potentially causing issues.
- __[CMake]__
    - Correctly set INSTALL_RPATH for target libraries
    - Split `mercury.pc` pkg config file into multiple `.pc` files for
    `mercury_util` and `na` to prevent from overlinking against those libraries
    when using pkg config.

## :warning: Known Issues

- __[NA OFI]__
    - [tcp/verbs;ofi_rxm] Using more than 256 peers requires `FI_UNIVERSE_SIZE` to be set.
- __[NA UCX]__
    - `NA_Addr_to_string()` cannot be used on non-listening processes to convert a self-address to a string.
