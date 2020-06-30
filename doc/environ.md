
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

Environment variables in this section apply to both the server side and the client side

### `DAOS_IO_MODE`

Control the DAOS IO mode: server dispatches modification RPCs to replicas or client does that, if it is the former case, whether enable DTX or not. `INTEGER`. Valid values are as following, default to 0 (server dispatches RPCs and enable DTX).

0: server dispatches RPCs, enable DTX.

1: server dispatches RPCs, disable DTX.

2: client disptaches RPCs, disable DTX.

### `DAOS_IO_BYPASS`

## Server

Environment variables in this section only apply to the server side.

These checksum algorithms are currently supported: `crc64` and `crc32`.

### `VOS_MEM_CLASS`

Memory class used by VOS. `STRING`. Default to persistent memory.

If the value is set to `DRAM`, all data will be stored in volatile memory; otherwise, all data will be stored to persistent memory.

### `VOS_BDEV_CLASS`

SPDK bdev class used by VOS. `STRING`. Default to NVMe bdev.

When testing on node without NVMe device available, it can be set to `MALLOC` or `AIO` to make VOS using SPDK malloc or AIO device.

### `IO_STAT_PERIOD`

Print SPDK bdev io statistics periodically. `INTEGER`. Default to 0 (disabled).

If it is set to N (non-zero), SPDK bdev io statistics will be printed on server console in every N seconds.

### `RDB_ELECTION_TIMEOUT`

Raft election timeout used by RDBs in milliseconds. `INTEGER`. Default to 7000 ms.

### `RDB_REQUEST_TIMEOUT`

Raft request timeout used by RDBs in milliseconds. `INTEGER`. Default to 3000 ms.

### `RDB_COMPACT_THRESHOLD`

Raft log compaction threshold in applied entries. `INTEGER`. Default to 256 entries.

If set to 0, Raft log entries will never be compacted.

### `DAOS_REBUILD`

Whether to start rebuilds when excluding targets. `BOOL2`. Default to true.

### `DAOS_MD_CAP`

Size of a metadata pmem pool/file in MBs. `INTEGER`. Default to 128 MB.

### `DAOS_START_POOL_SVC`

Whether to start existing pool services when starting a `daos_server`. `BOOL`. Default to true.

### `DAOS_IMPLICIT_PURGE`

Whether to aggregate unreferenced epochs. `BOOL`. Default to false.

### `DAOS_TARGET_OVERSUBSCRIBE`
Whether to accept target number oversubscribe for daos server. `BOOL`. Default to false.

### `DAOS_POOL_CREATE_TIMEOUT`

Pool target create timeout used during pool creations in seconds. `INTEGER`.
Default to CRT_TIMEOUT or 60 s if CRT_TIMEOUT is zero or not set.

## Client

Environment variables in this section only apply to the client side.

### `DAOS_IO_SRV_DISPATCH`

Whether to enable the server-side IO dispatch, in that case the replica IO will be sent to a leader shard which will dispatch to other shards. `BOOL`. Default to true.

## Debug System (Client & Server)

### `D_LOG_FILE`

DAOS debug logs (both server and client) are written to /tmp/daos.log by
default. This can be modified by setting this environment variable
("D_LOG_FILE=/tmp/daos_server").

### `DD_SUBSYS`

Used to specify which subsystems to enable. DD_SUBSYS can be set to individual
subsystems for finer-grained debugging ("DD_SUBSYS=vos"), multiple facilities
("DD_SUBSYS=eio,mgmt,misc,mem"), or all facilities ("DD_SUBSYS=all") which is
also the default setting. If a facility is not enabled, then only ERR messages
or more severe messages will print.

### `DD_STDERR`

Used to specify the priority level to output to stderr. Options in decreasing
priority level order: FATAL, CRIT, ERR, WARN, NOTE, INFO, DEBUG. By default, all
CRIT and more severe DAOS messages will log to stderr ("DD_STDERR=CRIT"), and
the default for CaRT/GURT is FATAL.

### `D_LOG_MASK`

Used to specify what type/level of logging will be present for either all of the
registered subsystems, or a select few. Options in decreasing priority level
order: FATAL, CRIT, ERR, WARN, NOTE, INFO, DEBUG.
DEBUG option is used to enable all logging (debug messages as well as all higher
priority level messages).
Note that if D_LOG_MASK is not set, it will default to logging all messages
excluding debug ("D_LOG_MASK=INFO").
EX: "D_LOG_MASK=DEBUG" This will set the logging level for all facilities to
DEBUG, meaning that all debug messages, as well as higher priority messages will
be logged (INFO, NOTE, WARN, ERR, CRIT, FATAL).
EX: "D_LOG_MASK=DEBUG,MEM=ERR,RPC=ERR" This will set the logging level to DEBUG
for all facilities except MEM & RPC (which will now only log ERR and higher
priority level messages, skipping all DEBUG, INFO, NOTE & WARN messages)

### `DD_MASK`

Used to enable different debug streams for finer-grained debug messages,
essentially allowing the user to specify an area of interest to debug (possibly
involving many different subsystems) as opposed to parsing through many lines of
generic DEBUG messages. All debug streams will be enabled by default
("DD_MASK=all"). Single debug masks can be set ("DD_MASK=trace") or multiple
masks ("DD_MASK=trace,test,mgmt").
Note that since these debug streams are strictly related to the debug log
messages, D_LOG_MASK must be set to DEBUG.
