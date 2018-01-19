
# DAOS Environment Variables

This file lists the environment variables used by DAOS. **Many of them are meant for development purposes only and may be removed or changed in the future.**

The description of each variable follows the following format:

> A short description. `Type`. Default to what behavior if not set.
>
> A longer description if necessary.

`Type` is defined by this table:

Type		| Values
--------------- | --------------------------------------------------------------
`BOOL`		| `0` means false; any other value means true
`BOOL2`		| `no` means false; any other value means true
`BOOL3`		| set to empty or any value means true; unset means false
`INTEGER`	| Non-negative decimal integer
`STRING`	| String

## Common

Environment variables in this section apply to both the server side and the client side.

### `DD_LOG`

### `DD_FAC`

### `DD_MASK`

### `DD_STDERR`

### `DD_TUNE_ALLOC`

### `DAOS_IO_BYPASS`

## Server

Environment variables in this section only apply to the server side.

### `VOS_CHECKSUM`

Checksum algorithm used by VOS. `STRING`. Default to disabling checksums.

These checksum algorithms are currently supported: `crc64` and `crc32`.

### `VOS_MEM_CLASS`

Memory class used by VOS. `STRING`. Default to persistent memory.

If the value is set to `DRAM`, all data will be stored in volatile memory; otherwise, all data will be stored to persistent memory.

### `RDB_ELECTION_TIMEOUT`

Raft election timeout used by RDBs in milliseconds. `INTEGER`. Default to 7000 ms.

### `RDB_REQUEST_TIMEOUT`

Raft request timeout used by RDBs in milliseconds. `INTEGER`. Default to 3000 ms.

### `DAOS_REBUILD`

Whether to start rebuilds when excluding targets. `BOOL2`. Default to true.

### `DAOS_MD_CAP`

Size of a metadata pmem pool/file in MBs. `INTEGER`. Default to 128 MB.

### `DAOS_START_POOL_SVC`

Whether to start existing pool services when starting a `daos_server`. `BOOL`. Default to true.

### `DAOS_IMPLICIT_PURGE`

Whether to aggregate unreferenced epochs. `BOOL3`. Default to false.

### `DAOS_PURGE_CREDITS`

Number of credits for probing object trees when aggregating unreferenced epochs. `INTGER`. Default to 1000.

## Client

Environment variables in this section only apply to the client side.

### `DAOS_SINGLETON_CLI`

Whether to run in the singleton mode, in which the client does not need to be launched by orterun. `BOOL`. Default to false.
