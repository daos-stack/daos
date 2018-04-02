# DAOS Debugging

D_LOG_FILE:
	DAOS debug logs are written by default to /tmp/daos.log, this can be
	modified by specifying a different path through D_LOG_FILE.

D_LOG_MASK:
	Used to set logging mask for either all subsystems (ie D_LOG_MASK=INFO
	will set all subsystems log level to INFO) or specific subsystems
	(ie D_LOG_MASK=ERR,RPC=DEBUG,BULK=INFO).

DD_MASK:
	Can enable different debug streams for finer-grained debugging per
	subsystem, for example "DD_MASK=trace" will log only debug messages
	with the bit mask DB_TRACE associated with them. Multiple streams can be
	set as well, for example: "DD_MASK=io,mem" will allow logging of a
	subset of debug messages related to io path (bit mask=DB_IO) and memory
	operations (bit mask=DB_MEM). Since these are debug masks, the
	subsystem/facility must have the debug log set ("D_LOG_MASK=DEBUG"),
	otherwise other priorities take precedence. The bit streams are passed
	as a parameter to D_DEBUG(mask, fmt, ...).

	Current DAOS debug masks:
		md/metadata, pl/placement, mgmt/management, epc/epoch,
		df/durafmt, rebuild
	Current GURT/common debug masks:
		any, all, mem, net, io, trace, test

DD_STDERR:
	User can specify the priority level to output to stderr, for example:
	"DD_STDERR=info" will log all DLOG_INFO priority messages to stderr.

DD_SUBSYS:
	User can specify which subsystems to enable.
	DAOS facilites disabled by default:
		common, tree, vos, utils, tests
	DAOS facilites enabled by default:
		null, client, server, rdb, pool, container, object, placement,
		rebuild, tier, mgmt
	DD_SUBSYS=all will enable all facilities.

More information available on debugging envs in CaRT README.
