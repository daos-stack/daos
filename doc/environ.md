
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

Whether to aggregate unreferenced epochs. `BOOL`. Default to false.

### `DAOS_PURGE_CREDITS`

Number of credits for probing object trees when aggregating unreferenced epochs. `INTGER`. Default to 1000.

## Client

Environment variables in this section only apply to the client side.

### `DAOS_SINGLETON_CLI`

Whether to run in the singleton mode, in which the client does not need to be launched by orterun. `BOOL`. Default to false.

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
messages, DD_LOG_MASK must be set to DEBUG.
