# Troubleshooting

## DAOS Errors

DAOS error numbering starts at 1000. The most common
errors are documented in the table below.

|DAOS Error|Value|Description
|-|-|-|
|DER_NO_PERM|1001|No permission
|DER_NO_HDL|1002|Invalid handle
|DER_INVAL|1003|Invalid parameters
|DER_NOSPACE|1007|No space left on storage target
|DER_NOSYS|1010|Function not implemented
|DER_IO|2001|Generic I/O error
|DER_ENOENT|2003|Entry not found
|DER_KEY2BIG|2012|Key is too large
|DER_IO_INVAL|2014|IO buffers can't match object extents
|DER_AGENT_INCOMPAT|2029|Agent is incompatible with libdaos

When an operation fails, DAOS returns a negative DER error. For a full
list of errors, please check
<https://github.com/daos-stack/cart/blob/master/src/include/daos_errno.h>
(DER_ERR_GURT_BASE is equal to 1000 and DER_ERR_DAOS_BASE is equal
to 2000).

The function d_errstr() is provided in the API to convert an error
number to an error message.

## Log Files

On the server side, there are three log files created as part of normal
server operations:

|Component|Config Parameter|Example Config Value|
|-|-|-|
|Control Plane|control_log_file|/tmp/daos_control.log|
|Data Plane|log_file|/tmp/daos_server.log|
|[Privileged Helper](https://daos-stack.github.io/admin/deployment/#elevated-privileges)|helper_log_file|/tmp/daos_admin.log|
|agent|log_file|/tmp/daos_agent.log|

### Control Plane Log

The default log level for the control plane is INFO. The following
levels may be set using the `control_log_mask` config parameter:

* DEBUG
* INFO
* ERROR

### Data Plane Log

Data Plane (`daos_io_server`) logging is configured on a per-instance
basis. In other words, each section under the `servers:` section must
have its own logging configuration. The `log_file` config parameter
is converted to a D_LOG_FILE environment variable value. For more
detail, please see the [Debugging System](#debugging-system)
section of this document.

### Privileged Helper Log

By default, the privileged helper only emits ERROR-level logging which
is captured by the control plane and included in that log. If the
`helper_log_file` parameter is set in the server config, then
DEBUG-level logging will be sent to the specified file.

### Daos Agent Log

If the `log_file` config parameter is set in the agent config, then
DEBUG-level logging will be sent to the specified file.


## Debugging System

DAOS uses the debug system defined in
[CaRT](https://github.com/daos-stack/cart) but more specifically the
GURT library. Default server log is "/tmp/daos.log" and client default
log is stdout, unless otherwise set by `D_LOG_FILE`.

### Registered Subsystems/Facilities

The debug logging system includes a series of subsystems or facilities
which define groups for related log messages (defined per source file).
There are common facilities which are defined in GURT, as well as other
facilities that can be defined on a per-project basis (such as those for
CaRT and DAOS). DD_SUBSYS can be used to set which subsystems to enable
logging. By default all subsystems are enabled ("DD_SUBSYS=all").

-   DAOS Facilities:
    common, tree, vos, client, server, rdb, pool, container, object,
    placement, rebuild, tier, mgmt, bio, tests

-   Common Facilities (GURT):
    MISC, MEM

-   CaRT Facilities:
    RPC, BULK, CORPC, GRP, LM, HG, ST, IV

### Priority Logging

All macros that output logs have a priority level, shown in descending
order below.

-   D_FATAL(fmt, ...) FATAL

-   D_CRIT(fmt, ...) CRIT

-   D_ERROR(fmt, ...) ERR

-   D_WARN(fmt, ...) WARN

-   D_NOTE(fmt, ...) NOTE

-   D_INFO(fmt, ...) INFO

-   D_DEBUG(mask, fmt, ...) DEBUG

The priority level that outputs to stderr is set with DD_STDERR. By
default in DAOS (specific to the project), this is set to CRIT
("DD_STDERR=CRIT") meaning that all CRIT and more severe log messages
will dump to stderr. However, this is separate from the priority of
logging to "/tmp/daos.log". The priority level of logging can be set
with D_LOG_MASK, which by default is set to INFO
("D_LOG_MASK=INFO"), which will result in all messages excluding DEBUG
messages being logged. D_LOG_MASK can also be used to specify the
level of logging on a per-subsystem basis as well
("D_LOG_MASK=DEBUG,MEM=ERR").

### Debug Masks/Streams:

DEBUG messages account for a majority of the log messages, and
finer-granularity might be desired. Mask bits are set as the first
argument passed in D_DEBUG(mask, ...). To accomplish this, DD_MASK can
be set to enable different debug streams. Similar to facilities, there
are common debug streams defined in GURT, as well as other streams that
can be defined on a per-project basis (CaRT and DAOS). All debug streams
are enabled by default ("DD_MASK=all").

-   DAOS Debug Masks:

    -   md = metadata operations

    -   pl = placement operations

    -   mgmt = pool management

    -   epc = epoch system

    -   df = durable format

    -   rebuild = rebuild process

    -   daos_default = (group mask) io, md, pl, and rebuild operations

-   Common Debug Masks (GURT):

    -   any = generic messages, no classification

    -   trace = function trace, tree/hash/lru operations

    -   mem = memory operations

    -   net = network operations

    -   io = object I/Otest = test programs

### Common Use Cases

-   Generic setup for all messages (default settings)

        $ D_LOG_MASK=DEBUG
        $ DD_SUBSYS=all
        $ DD_MASK=all

-   Disable all logs for performance tuning

        $ D_LOG_MASK=ERR -> will only log error messages from all facilities
        $ D_LOG_MASK=FATAL -> will only log system fatal messages

-   Disable a noisy debug logging subsystem

        $ D_LOG_MASK=DEBUG,MEM=ERR -> disables MEM facility by
        restricting all logs from that facility to ERROR or higher priority only

-   Enable a subset of facilities of interest

        $ DD_SUBSYS=rpc,tests
        $ D_LOG_MASK=DEBUG -> required to see logs for RPC and TESTS
        less severe than INFO (the majority of log messages)

-   Fine-tune the debug messages by setting a debug mask

        $ D_LOG_MASK=DEBUG
        $ DD_MASK=mgmt -> only logs DEBUG messages related to pool
        management

Refer to the DAOS Environment Variables document for
more information about the debug system environment.

## Common DAOS Problems

When DER_AGENT_INCOMPAT is received, it means that the client library libdaos.so
is likely mismatched with the DAOS Agent.  The libdaos.so, DAOS Agent and DAOS
Server must be built from compatible sources so that the GetAttachInfo protocol
is the same between each component.  Depending on your situation, you will need
to either update the DAOS Agent or the libdaos.so to the newer version in order
to maintain compatibility with each other.

## Bug Report

Bugs should be reported through our issue tracker[^1] with a test case
to reproduce the issue (when applicable) and debug logs.

After creating a ticket, logs should be gathered from the locations
described in the [Log Files](#log-files) section of this document and
attached to the ticket.

To avoid problems with attaching large files, please attach the logs
in a compressed container format, such as .zip or .tar.bz2.

[^1]: https://jira.hpdd.intel.com
