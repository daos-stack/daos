# DAOS Debugging

DAOS uses the debug system defined in
[CaRT](https://github.com/daos-stack/daos/tree/master/src/cart),
specifically the GURT library. 
Both server and client default log is `stdout`, 
unless otherwise set by `D_LOG_FILE` environment variable (client) or
`log_file` config parameter (server).

## Registered Subsystems/Facilities

The debug logging system includes a series of subsystems or facilities which
define groups for related log messages (defined per source file). There are
common facilities which are defined in GURT, as well as other facilities that
can be defined on a per-project basis (such as those for CaRT and DAOS).
`DD_SUBSYS` can be used to set which subsystems to enable logging for. By
default all subsystems are enabled (`DD_SUBSYS=all`).
- DAOS Facilities: [array, kv, common, tree, vos, client, server, rdb, rsvc,
		    pool, container, object, placement, rebuild, mgmt, bio, tests, dfs,
		    duns, drpc, security, dtx, dfuse, il, csum]
- Common Facilities (GURT): [MISC, MEM, SWIM, TELEM]
- CaRT Facilities: [RPC, BULK, CORPC, GRP, HG, ST, IV, CTL]

## Priority Logging

All macros which output logs have a priority level, shown in descending order
below.
- D_FATAL(fmt, ...)		FATAL
- D_CRIT(fmt, ...)		CRIT
- D_ERROR(fmt, ...)		ERR
- D_WARN(fmt, ...)		WARN
- D_NOTE(fmt, ...)		NOTE
- D_INFO(fmt, ...)		INFO
- D_DEBUG(mask, fmt, ...)	DEBUG

The priority level that outputs to stderr can be set with `DD_STDERR`. By
default in DAOS (specific to project), this is set to CRIT (`DD_STDERR=CRIT`)
meaning that all CRIT and more severe log messages will dump to stderr. This
however is separate from the priority of logging to `/tmp/daos.log`. The
priority level of logging can be set with `D_LOG_MASK`, which by default is set
to INFO (`D_LOG_MASK=INFO`), which will result in all messages excluding DEBUG
messages being logged. `D_LOG_MASK` can also be used to specify the level of
logging on a per-subsystem basis as well (`D_LOG_MASK="DEBUG,MEM=ERR"`).

## Debug Masks/Streams:

DEBUG messages account for a majority of the log messages, and finer-granularity
might be desired. Mask bits are set as the first argument passed in
D_DEBUG(mask, ...). In order to accomplish this, `DD_MASK` can be set to enable
different debug streams. Similar to facilities, there are common debug streams
defined in GURT, as well as other streams that can defined on a per-project
basis (CaRT and DAOS). All debug streams are enabled by default (`DD_MASK=all`).
- DAOS Debug Masks:
	- md = metadata operations
	- pl = placement operations
	- mgmt = pool management
	- epc = epoch system
	- df = durable format
	- rebuild = rebuild process
	- group_default = (group mask) io, md, pl, and rebuild operations
	- group_metadata = (group mask) group_default and mgmt operations
	- group_metadata_only = (group mask) mgmt and md operations
- Common Debug Masks (GURT):
	- any = generic messages, no classification
	- trace = function trace, tree/hash/lru operations
	- mem = memory operations
	- net = network operations
	- io = object I/O
	- test = test programs

## Common Use Cases

Please note: where in these examples the export command is shown setting an environment variable,
this is intended to convey either that the variable is actually set (for the client environment), or
configured for the engines in the `daos_server.yml` file (`log_mask` per engine, and env_vars
values per engine for the `DD_SUBSYS` and `DD_MASK` variable assignments).

- Generic setup for all messages (default settings)
```bash
  $ export D_LOG_MASK=DEBUG
  $ export DD_SUBSYS=all
  $ export DD_MASK=all
```

- Disable all logs for performance tuning
```bash
  $ export D_LOG_MASK=ERR   # -> will only log error messages from all facilities
  $ export D_LOG_MASK=FATAL # -> will only log system fatal messages
```

- Disable a noisy debug logging subsystem
```bash
  $ export D_LOG_MASK=DEBUG,MEM=ERR # -> disables MEM facility by restricting all logs
                                    # from that facility to ERROR or higher priority only
  $ export D_LOG_MASK=DEBUG,SWIM=ERR,RPC=ERR,HG=ERR # -> disables SWIM and RPC/HG facilities
```

- Gather daos metadata logs if a pool/container resource problem is observed, using the provided group mask
```bash
  $ export D_LOG_MASK=DEBUG,MEM=ERR # log at DEBUG level from all facilities except MEM
  $ export DD_MASK=group_metadata   # limit logging to include only streams (mgmt, plus defaults from group_default)
```

- Enable a subset of facilities of interest
```bash
  $ export DD_SUBSYS=rpc,tests
  $ export D_LOG_MASK=DEBUG    # -> required to see logs for RPC and TESTS less severe
                               #    than INFO (majority of log messages)
```

- Fine-tune the debug messages by setting a debug mask
```bash
  $ export D_LOG_MASK=DEBUG
  $ export DD_MASK=mgmt     # -> only logs DEBUG messages related to pool management
```

**See the [Troubleshooting](admin/troubleshooting.md) documentation for more info
about debug system environment.**
