# Troubleshooting

## DAOS Errors

DAOS error numbering starts at 1000. The most common
errors are documented in the table below.

|DAOS Error|Value|Description
|-|-|-|
|DER_NO_PERM|1001|No permission
|DER_NO_HDL|1002|Invalid handle
|DER_INVAL|1003|Invalid parameters
|DER_EXIST|1004|Entity already exists
|DER_NONEXIST|1005|The specified entity does not exist
|DER_UNREACH|1006|Unreachable node
|DER_NOSPACE|1007|No space left on storage target
|DER_ALREADY|1008|Operation already performed
|DER_NOMEM|1009|Out of memory
|DER_NOSYS|1010|Function not implemented
|DER_TIMEDOUT|1011|Time out
|DER_BUSY|1012|Device or resource busy
|DER_AGAIN|1013|Try again
|DER_PROTO|1014|Incompatible protocol
|DER_UNINIT|1015|Device or resource not initialized
|DER_TRUNC|1016|Buffer too short
|DER_OVERFLOW|1017|Data too long for defined data type or buffer size
|DER_CANCELED|1018|Operation canceled
|DER_OOG|1019|Out of group or member list
|DER_HG|1020|Transport layer mercury error
|DER_MISC|1025|Miscellaneous error
|DER_BADPATH|1026|Bad path name
|DER_NOTDIR|1027|Not a directory
|DER_EVICTED|1032|Rank has been evicted
|DER_DOS|1034|Denial of service
|DER_BAD_TARGET|1035|Incorrect target for the RPC
|DER_HLC_SYNC|1037|HLC synchronization error
|DER_IO|2001|Generic I/O error
|DER_ENOENT|2003|Entry not found
|DER_NOTYPE|2004|Unknown object type
|DER_NOSCHEMA|2005|Unknown object schema
|DER_KEY2BIG|2012|Key is too large
|DER_REC2BIG|2013|Record is too large
|DER_IO_INVAL|2014|IO buffers can't match object extents
|DER_EQ_BUSY|2015|Event queue is busy
|DER_SHUTDOWN|2017|Service should shut down
|DER_INPROGRESS|2018|Operation now in progress
|DER_NOTREPLICA|2020|Not a service replica
|DER_CSUM|2021|Checksum error
|DER_REC_SIZE|2024|Record size error
|DER_TX_RESTART|2025|Transaction should restart
|DER_DATA_LOSS|2026|Data lost or not recoverable
|DER_TX_BUSY|2028|TX is not committed
|DER_AGENT_INCOMPAT|2029|Agent is incompatible with libdaos

When an operation fails, DAOS returns a negative DER error.
For a complete list of errors, please check
<https://github.com/daos-stack/daos/blob/release/2.2/src/include/daos_errno.h>
(`DER_ERR_GURT_BASE` is equal to 1000, and `DER_ERR_DAOS_BASE` is equal
to 2000).

The function d_errstr() is provided in the API to convert an error
number to an error message.

## Log Files

On the server-side, there are three log files created as part of normal
server operations:

|Component|Config Parameter|Example Config Value|
|-|-|-|
|Control Plane|control_log_file|/tmp/daos_server.log|
|Data Plane|log_file|/tmp/daos_engine.\*.log|
|[Privileged Helper](https://docs.daos.io/v2.2/admin/deployment/#elevated-privileges)|helper_log_file|/tmp/daos_admin.log|
|agent|log_file|/tmp/daos_agent.log|

### Control Plane Log

The default log level for the control plane is INFO. The following
levels may be set using the `control_log_mask` config parameter:

* DEBUG
* INFO
* ERROR

### Data Plane Log

Data Plane (`daos_engine`) logging is configured on a per-instance
basis. In other words, each section under the `servers:` section must
have its own logging configuration. The `log_file` config parameter
is converted to a D_LOG_FILE environment variable value. For more
detail, please see the [Debugging System](#debugging-system)
section of this document.

### Privileged Helper Log

By default, the privileged helper only emits ERROR-level logging, which
is captured by the control plane and included in that log. If the
`helper_log_file` parameter is set in the server config, then
DEBUG-level logging will be sent to the specified file.

### Daos Agent Log

If the `log_file` config parameter is set in the agent config, then
DEBUG-level logging will be sent to the specified file.

## Debugging System

DAOS uses the debug system defined in
[CaRT](https://github.com/daos-stack/daos/tree/release/2.2/src/cart),
specifically, the GURT library.
Both server and client default log is `stdout` unless
otherwise set by the `D_LOG_FILE` environment variable (client) or
`log_file` config parameter (server).

### Registered Subsystems/Facilities

The debug logging system includes a series of subsystems or facilities
which define groups for related log messages (defined per source file).
There are common facilities that are defined in GURT, as well as to other
facilities that can be defined on a per-project basis (such as those for
CaRT and DAOS). DD_SUBSYS can be used to set which subsystems to enable
logging. By default, all subsystems are enabled ("DD_SUBSYS=all").

* DAOS Facilities:
    array, kV, common, tree, vos, client, server, rdb, rsvc, pool, container,
    object, placement, rebuild, tier, mgmt, bio, tests, dfs, duns, drpc,
    security, dtx, dfuse, il, csum
* Common Facilities (GURT):
    MISC, MEM, SWIM, TELEM
* CaRT Facilities:
    RPC, BULK, CORPC, GRP, HG, ST, IV, CTL

### Priority Logging

The priority level that outputs to stderr is set with DD_STDERR. By
Default, in DAOS (specific to the project), this is set to CRIT
("DD_STDERR=CRIT") meaning that all CRIT and more severe log messages
will dump to stderr. However, this is separate from the priority of
logging to "/tmp/daos.log". The priority level of logging can be set
with D_LOG_MASK, which by default is set to INFO
("D_LOG_MASK=INFO"), which will result in all messages, excluding DEBUG
messages, being logged. D_LOG_MASK can also be used to specify the
level of logging on a per-subsystem basis as well
("D_LOG_MASK=DEBUG,MEM=ERR").

### Debug Masks/Streams

DEBUG messages account for most log messages, and
finer granularity might be desired. Mask bits are set as the first
argument passed in D_DEBUG(mask, ...). To accomplish this, DD_MASK can
be set to enable different debug streams. Similar to facilities, there
are common to debug streams defined in GURT, and other streams that
can be defined on a per-project basis (CaRT and DAOS). All debug streams
are enabled by default ("DD_MASK=all"). Convenience "group mask" values
are defined for common use cases and convenience and consist of a
composition of multiple individual bits.

* DAOS Debug Masks:
  * md = metadata operations
  * pl = placement operations
  * mgmt = pool management
  * epc = epoch system
  * df = durable format
  * rebuild = rebuild process
  * group_default = (group mask) io, md, pl, and rebuild operations
  * group_metadata_only = (group mask) mgmt, md operations
  * group_metadata = (group mask) group_default plus mgmt operations
* Common Debug Masks (GURT):
  * any = generic messages, no classification
  * trace = function trace, tree/hash/lru operations
  * mem = memory operations
  * net = network operations
  * io = object I/Otest = test programs

### Common Use Cases

Please note wherein these examples, the export command is shown setting an environment variable,
this is intended to convey either that the variable is set (for the client environment), or
configured for the engines in the `daos_server.yml` file (`log_mask` per engine, and env_vars
values per engine for the `DD_SUBSYS` and `DD_MASK` variable assignments).

* Generic setup for all messages (default settings)

  ```sh
  D_LOG_MASK=DEBUG
  DD_SUBSYS=all
  DD_MASK=all
  ```

* Disable all logs for performance tuning

  ```sh
  D_LOG_MASK=ERR -> will only log error messages from all facilities
  D_LOG_MASK=FATAL -> will only log fatal system messages
  ```

* Gather daos metadata logs if a pool/container resource problem is observed, using the provided group mask

  ```sh
  D_LOG_MASK=DEBUG -> log at DEBUG level from all facilities
  DD_MASK=group_metadata -> limit logging to include deault and metadata-specific streams. Or, specify DD_MASK=group_metadata_only for just metadata-specific log entries.
  ```

* Disable a noisy debug logging subsystem

  ```sh
  D_LOG_MASK=DEBUG,MEM=ERR -> disables MEM facility by
  restricting all logs from that facility to ERROR or higher priority only
  D_LOG_MASK=DEBUG,SWIM=ERR,RPC=ERR,HG=ERR -> disables SWIM and RPC/HG facilities
  ```

* Enable a subset of facilities of interest

  ```sh
  DD_SUBSYS=rpc,tests
  D_LOG_MASK=DEBUG -> required to see logs for RPC and TESTS
  less severe than INFO (the majority of log messages)
  ```

* Fine-tune the debug messages by setting a debug mask

  ```sh
  D_LOG_MASK=DEBUG
  DD_MASK=mgmt -> only logs DEBUG messages related to pool
  management
  ```

Refer to the DAOS Environment Variables document for
more information about the debug system environment.

## Common DAOS Problems

### Incompatible Agent

When DER_AGENT_INCOMPAT is received, it means that the client library libdaos.so
is likely mismatched with the DAOS Agent.  The libdaos.so, DAOS Agent and DAOS
Server must be built from compatible sources so that the GetAttachInfo protocol
is the same between each component.  Depending on your situation, you will need
to either update the DAOS Agent or the libdaos.so to the newer version in order
to maintain compatibility with each other.

### HLC Sync

When DER_HLC_SYNC is received, it means that sender and receiver HLC timestamps
are off by more than the maximum allowed system clock offset (1 second by default).

To correct this situation, synchronize all server clocks to the same
reference time, using services like NTP.

### Shared Memory Errors

When DER_SHMEM_PERMS is received, this I/O Engine lacks the
permissions to access the shared memory segment left behind by a previous run of
the I/O Engine on the same machine.  This happens when the I/O Engine fails to
remove the shared memory segment upon shutdown, and there is a mismatch between
the user/group used to launch the I/O Engine between these successive runs.  To
remedy the problem, manually identify the shared memory segment and remove it.

Issue `ipcs` to view the Shared Memory Segments.  The output will show a
list of segments organized by `key`.

```bash
ipcs

------ Message Queues --------
key        msqid      owner      perms      used-bytes   messages

------ Shared Memory Segments --------
key        shmid      owner      perms      bytes      nattch     status
0xffffffff 49938432   root       666        40         0
0x10242048 98598913   jbrosenz   660        1048576    0
0x10242049 98631682   jbrosenz   660        1048576    0

------ Semaphore Arrays --------
key        semid      owner      perms      nsems
```

Shared Memory Segments with keys [0x10242048 .. (0x10242048 + number of I/O
Engines running)] are the segments that must be removed.  Use `ipcrm` to
remove the segment.

For example, to remove the shared memory segment left behind by I/O Engine
instance 0, issue:

  ```bash
  sudo ipcrm -M 0x10242048
  ```

To remove the shared memory segment left behind by I/O Engine instance 1, issue:

  ```bash
  sudo ipcrm -M 0x10242049
  ```

### Server Start Issues

1. Read the log located in the `control_log_file`.
1. Verify that the `daos_server` process is not currently running.
1. Check the SCM device path in /dev.
1. Verify the PCI addresses using `dmg storage scan`.

    !!! note
        A server must be started with minimum setup.
        You can also obtain the addresses with `daos_server storage scan`.

1. Format the SCMs defined in the config file.
1. Generate the config file using `dmg config generate`. The various requirements will be populated without a syntax error.
1. Try starting with `allow_insecure: true`. This will rule out the credential certificate issue.
1. Verify that the `access_points` host is accessible and the port is not used.
1. Check the `provider` entry. See the "Network Scan and Configuration" section of the admin guide to determine the right provider.
1. Check `fabric_iface` in `engines`. They should be available and enabled.
1. Check that `socket_dir` is writeable by the daos_server.

### Errors creating a Pool

1. Check which engine rank you want to create a pool in with `dmg system query --verbose` and verify their State is Joined.
1. `DER_NOSPACE(-1007)` appears: Check the size of the NVMe and PMEM. Next, check the size of the existing pool. Then check that this new pool will fit into the remaining disk space.

### Problems creating a container

1. Check that the path to daos is your intended binary. It's usually `/usr/bin/daos`.
1. When the server configuration is changed, it's necessary to restart the agent.
1. `DER_UNREACH(-1006)`: Check the socket ID consistency between PMEM and NVMe. First, determine which socket you're using with `daos_server network scan -p all`. e.g., if the interface you're using in the engine section is eth0, find which NUMA Socket it belongs to. Next, determine the disks you can use with this socket by calling `daos_server storage scan` or `dmg storage scan`. e.g., if eth0 belongs to NUMA Socket 0, use only the disks with 0 in the Socket ID column.
1. Check the interface used in the server config (`fabric_iface`) also exists in the client and can communicate with the server.
1. Check the access_points of the agent config points to the correct server host.
1. Call `daos pool query` and check that the pool exists and has free space.

### Applications run slow

Verify if you're using Infiniband for `fabric_iface`: in the server config. The IO will be significantly slower with Ethernet.

## Common Errors and Workarounds

### Use dmg command without daos_admin privilege

  ```bash
  # Error message or timeout after dmg system query
  $ dmg system query
  ERROR: dmg: Unable to load Certificate Data: could not load cert: stat /etc/daos/certs/admin.crt: no such file or directory
  
  # Workaround
    # 1. Make sure the admin-host /etc/daos/daos_control.yml is correctly configured.
      # including:
      # hostlist: <daos_server_lists>
      # port: <port_num>
      # transport\config:
      # allow_insecure: <true/false>
      # ca\cert: /etc/daos/certs/daosCA.crt
      # cert: /etc/daos/certs/admin.crt
      # key: /etc/daos/certs/admin.key
    # 2. Make sure the admin-host allow_insecure mode matches the applicable servers.
```

### use the daos command before daos_agent started

  ```bash
  $ daos cont create $DAOS_POOL
  daos ERR  src/common/drpc.c:217 unixcomm_connect() Failed to connect to /var/run/daos_agent/daos_agent.sock, errno=2(No such file or directory)
  mgmt ERR  src/mgmt/cli_mgmt.c:222 get_attach_info() failed to connect to /var/run/daos_agent/daos_agent.sock DER_MISC(-1025): 'Miscellaneous error'
  failed to initialize daos: Miscellaneous error (-1025)
    # Work around to check for daos_agent certification and start daos_agent
      #check for /etc/daos/certs/daosCA.crt, agent.crt and agent.key
        $ sudo systemctl enable daos_agent.service
        $ sudo systemctl start daos_agent.service
```

### use the daos command with invalid or wrong parameters

  ```bash
   # Lack of providing daos pool_uuid
   $ daos pool list-cont
   pool UUID required
   rc: 2
   daos command (v1.2), libdaos 1.2.0
   usage: daos RESOURCE COMMAND [OPTIONS]
   resources:
     pool             pool
     container (cont) container
     filesystem (fs)  copy to and from a POSIX filesystem
     object (obj)     object
     shell            Interactive obj ctl shell for DAOS
     version          print command version
     help             print this message and exit
   use 'daos help RESOURCE' for resource specifics

   # Invalid sub-command cont-list
   $ daos pool cont-list --pool=$DAOS_POOL
   invalid pool command: cont-list
   error parsing command line arguments
   daos command (v1.2), libdaos 1.2.0
   usage: daos RESOURCE COMMAND [OPTIONS]
   resources:
     pool             pool
     container (cont) container
     filesystem (fs)  copy to and from a POSIX filesystem
     object (obj)     object
     shell            Interactive obj ctl shell for DAOS
     version          print command version
     help             print this message and exit
   use 'daos help RESOURCE' for resource specifics

   # Working daos pool command
   $ daos pool list-cont --pool=$DAOS_POOL
   bc4fe707-7470-4b7d-83bf-face75cc98fc
   ```

## dmg pool create failed due to no space

  ```bash
  $ dmg pool create --size=50G mypool
  Creating DAOS pool with automatic storage allocation: 50 GB NVMe + 6.00% SCM
  ERROR: dmg: pool create failed: DER_NOSPACE(-1007): No space on storage target

  # Workaround: dmg storage query scan to find currently available storage
  dmg storage query usage
  Hosts  SCM-Total SCM-Free SCM-Used NVMe-Total NVMe-Free NVMe-Used
  -----  --------- -------- -------- ---------- --------- ---------
  boro-8 17 GB     6.0 GB   65 %     0 B        0 B       N/A

  $ dmg pool create --size=2G mypool
  Creating DAOS pool with automatic storage allocation: 2.0 GB NVMe + 6.00% SCM
  Pool created with 100.00% SCM/NVMe ratio
  -----------------------------------------
    UUID          : b5ce2954-3f3e-4519-be04-ea298d776132
    Service Ranks : 0
    Storage Ranks : 0
    Total Size    : 2.0 GB
    SCM           : 2.0 GB (2.0 GB / rank)
    NVMe          : 0 B (0 B / rank)

  $ dmg storage query usage
  Hosts  SCM-Total SCM-Free SCM-Used NVMe-Total NVMe-Free NVMe-Used
  -----  --------- -------- -------- ---------- --------- ---------
  boro-8 17 GB     2.9 GB   83 %     0 B        0 B       N/A

### dmg pool destroy timeout

 # dmg pool destroy Timeout or failed due to pool has active container(s)
 # Workaround pool destroy --force option

  $ dmg pool destroy --pool=$DAOS_POOL --force
  Pool-destroy command succeeded

## Bug Report

Bugs should be reported through our issue tracker[^1] with a test case
to reproduce the issue (when applicable) and debug logs.

After creating a ticket, logs should be gathered from the locations
described in the [Log Files](#log-files) section of this document and
attached to the ticket.

To avoid problems with attaching large files, please attach the logs
in a compressed container format, such as .zip or .tar.bz2.

[^1]: http://jira.daos.io
