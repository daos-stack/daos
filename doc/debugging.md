# DAOS Debugging

DAOS uses the debug system defined in [CaRT](https://github.com/daos-stack/cart)
but more specifically the GURT library. The default server log is `/tmp/daos.log`
and the default client log is `stdout`, unless otherwise set by `D_LOG_FILE`.

## Registered Subsystems/Facilities

The debug logging system includes a series of subsystems or facilities which
define groups for related log messages (defined per source file). There are
common facilities which are defined in GURT, as well as other facilities that
can be defined on a per-project basis (such as those for CaRT and DAOS).
`DD_SUBSYS` can be used to set which subsystems to enable logging for. By
default all subsystems are enabled (`DD_SUBSYS=all`).
- DAOS Facilities: [common, tree, vos, client, server, rdb, pool, container,
		    object, placement, rebuild, tier, mgmt, eio, tests]
- Common Facilities (GURT): [MISC, MEM]
- CaRT Facilities: [RPC, BULK, CORPC, GRP, LM, HG, ST, IV]

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
	- daos_default = (group mask) io, md, pl, and rebuild operations
- Common Debug Masks (GURT):
	- any = generic messages, no classification
	- trace = function trace, tree/hash/lru operations
	- mem = memory operations
	- net = network operations
	- io = object I/O
	- test = test programs

## Common Use Cases

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

**See the [DAOS Environment Variables](./environ.md) documentation for more info
about debug system environment.**
