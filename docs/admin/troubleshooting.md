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
For a full list of errors, please check
<https://github.com/daos-stack/daos/blob/master/src/include/daos_errno.h>
(`DER_ERR_GURT_BASE` is equal to 1000, and `DER_ERR_DAOS_BASE` is equal
to 2000).

The function d_errstr() is provided in the API to convert an error
number to an error message.

## Log Files

On the server side, there are three log files created as part of normal
server operations:

|Component|Config Parameter|Example Config Value|
|-|-|-|
|Control Plane|control_log_file|/tmp/daos_server.log|
|Data Plane|log_file|/tmp/daos_engine.\*.log|
|[Privileged Helper](https://docs.daos.io/v2.6/admin/predeployment_check/#privileged-helper)|helper_log_file|/tmp/daos_admin.log|
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

By default, the privileged helper only emits ERROR-level logging which
is captured by the control plane and included in that log. If the
`helper_log_file` parameter is set in the server config, then
DEBUG-level logging will be sent to the specified file.

### Daos Agent Log

If the `log_file` config parameter is set in the agent config, then
DEBUG-level logging will be sent to the specified file.

## Debugging System

DAOS uses the debug system defined in
[CaRT](https://github.com/daos-stack/daos/tree/master/src/cart),
specifically the GURT library.
Both server and client default log is `stdout`, unless
otherwise set by `D_LOG_FILE` environment variable (client) or
`log_file` config parameter (server).

### Registered Subsystems/Facilities

The debug logging system includes a series of subsystems or facilities
which define groups for related log messages (defined per source file).
There are common facilities which are defined in GURT, as well as other
facilities that can be defined on a per-project basis (such as those for
CaRT and DAOS). DD_SUBSYS can be used to set which subsystems to enable
logging. By default all subsystems are enabled ("DD_SUBSYS=all").

-   DAOS Facilities:
    daos, array, kv, common, tree, vos, client, server, rdb, rsvc, pool, container, object,
    placement, rebuild, mgmt, bio, tests, dfs, duns, drpc, security, dtx, dfuse, il, csum, stack

-   Common Facilities (GURT):
    misc, mem, swim, fi, telem

-   CaRT Facilities:
    crt, rpc, bulk, corpc, grp, lm, hg, external, st, iv, ctl

### Priority Logging

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

-   Log Levels:
    debug, dbug, info, note, warn, error, err, crit, alrt, fatal, emrg, emit

Note: debug == dbug, error == err and fatal == emrg.

### Debug Masks/Streams:

DEBUG messages account for a majority of the log messages, and
finer-granularity might be desired. Mask bits are set as the first
argument passed in D_DEBUG(mask, ...). To accomplish this, DD_MASK can
be set to enable different debug streams. Similar to facilities, there
are common debug streams defined in GURT, as well as other streams that
can be defined on a per-project basis (CaRT and DAOS). All debug streams
are enabled by default ("DD_MASK=all"). Convenience "group mask" values
are defined for common use cases and convenience, and consist of a
composition of multiple individual bits.

-   DAOS Debug Masks:

    -   md = metadata operations

    -   pl = placement operations

    -   mgmt = pool management

    -   epc = epoch system

    -   df = durable format

    -   rebuild = rebuild process

    -   sec = security

    -   csum = checksum

    -   group_default = (group mask) io, md, pl, and rebuild operations

    -   group_metadata = (group mask) group_default plus mgmt operations

    -   group_metadata_only = (group mask) mgmt, md operations

-   Common Debug Masks (GURT):

    -   any = generic messages, no classification

    -   trace = function trace, tree/hash/lru operations

    -   mem = memory operations

    -   net = network operations

    -   io = object I/O

    -   test = test programs

### Common Use Cases

Please note: where in these examples the export command is shown setting an environment variable,
this is intended to convey either that the variable is actually set (for the client environment), or
configured for the engines in the `daos_server.yml` file (`log_mask` per engine, and env_vars
values per engine for the `DD_SUBSYS` and `DD_MASK` variable assignments).

-   Generic setup for all messages (default settings)

        D_LOG_MASK=DEBUG
        DD_SUBSYS=all
        DD_MASK=all

-   Disable all logs for performance tuning

        D_LOG_MASK=ERR -> will only log error messages from all facilities
        D_LOG_MASK=FATAL -> will only log system fatal messages

-   Gather daos metadata logs if a pool/container resource problem is observed, using the provided group mask

        D_LOG_MASK=DEBUG -> log at DEBUG level from all facilities
        DD_MASK=group_metadata -> limit logging to include default and metadata-specific streams. Or, specify DD_MASK=group_metadata_only for just metadata-specific log entries.

-   Disable a noisy debug logging subsystem

        D_LOG_MASK=DEBUG,MEM=ERR -> disables MEM facility by
        restricting all logs from that facility to ERROR or higher priority only
        D_LOG_MASK=DEBUG,SWIM=ERR,RPC=ERR,HG=ERR -> disables SWIM and RPC/HG facilities

-   Enable a subset of facilities of interest

        DD_SUBSYS=rpc,tests
        D_LOG_MASK=DEBUG -> required to see logs for RPC and TESTS
        less severe than INFO (the majority of log messages)

-   Fine-tune the debug messages by setting a debug mask

        D_LOG_MASK=DEBUG
        DD_MASK=mgmt -> only logs DEBUG messages related to pool
        management

Refer to the DAOS Environment Variables document for
more information about the debug system environment.

## Common DAOS Problems
### Incompatible Agent ####
When DER_AGENT_INCOMPAT is received, it means that the client library libdaos.so
is likely mismatched with the DAOS Agent.  The libdaos.so, DAOS Agent and DAOS
Server must be built from compatible sources so that the GetAttachInfo protocol
is the same between each component.  Depending on your situation, you will need
to either update the DAOS Agent or the libdaos.so to the newer version in order
to maintain compatibility with each other.

### HLC Sync ###
When DER_HLC_SYNC is received, it means that sender and receiver HLC timestamps
are off by more than maximum allowed system clock offset (1 second by default).

In order to correct this situation synchronize all server clocks to the same
reference time, using services like NTP.

### Shared Memory Errors ###
When DER_SHMEM_PERMS is received it means that this I/O Engine lacked the
permissions to access the shared memory megment left behind by a previous run of
the I/O Engine on the same machine.  This happens when the I/O Engine fails to
remove the shared memory segment upon shutdown, and, there is a mismatch between
the user/group used to launch the I/O Engine between these successive runs.  To
remedy the problem, manually identify the shared memory segment and remove it.

Issue ```ipcs``` to view the Shared Memory Segments.  The output will show a
list of segments organized by ```key```.

```
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
Engines running)] are the segments that must be removed.  Use ```ipcrm``` to
remove the segment.

For example, to remove the shared memory segment left behind by I/O Engine
instance 0, issue:
```
sudo ipcrm -M 0x10242048
```
To remove the shared memory segment left behind by I/O Engine instance 1, issue:
```
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
1. Check the `provider` entry. See the "Network Scan and Configuration" section of the admin guide for determining the right provider to use.
1. Check `fabric_iface` in `engines`. They should be available and enabled.
1. Check that `socket_dir` is writeable by the daos_server.

### Errors creating a Pool
1. Check which engine rank you want to create a pool in with `dmg system query --verbose` and verify their State is Joined.
1. `DER_NOSPACE(-1007)` appears: Check the size of the NVMe and PMem. Next, check the size of the existing pool. Then check that this new pool being created will fit into the remaining disk space.

### Problems creating a container
1. Check that the path to daos is your intended binary. It's usually `/usr/bin/daos`.
1. When the server configuration is changed, it's necessary to restart the agent.
1. `DER_UNREACH(-1006)`: Check the socket ID consistency between PMem and NVMe. First, determine which socket you're using with `daos_server network scan -p all`. e.g., if the interface you're using in the engine section is eth0, find which NUMA Socket it belongs to. Next, determine the disks you can use with this socket by calling `daos_server storage scan` or `dmg storage scan`. e.g., if eth0 belongs to NUMA Socket 0, use only the disks with 0 in the Socket ID column.
1. Check the interface used in the server config (`fabric_iface`) also exists in the client and can communicate with the server.
1. Check the access_points of the agent config points to the correct server host.
1. Call `daos pool query` and check that the pool exists and has free space.

### Applications run slow
Verify if you're using Infiniband for `fabric_iface`: in the server config. The IO will be significantly slower with Ethernet.

## Common Errors and Workarounds

### Use dmg command without daos_server_helper privilege
```
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
			# ca_cert: /etc/daos/certs/daosCA.crt
			# cert: /etc/daos/certs/admin.crt
			# key: /etc/daos/certs/admin.key

# 2. Make sure the admin-host allow_insecure mode matches the applicable servers.
```
### use daos command before daos_agent started
```
$ daos cont create $DAOS_POOL
daos ERR  src/common/drpc.c:217 unixcomm_connect() Failed to connect to /var/run/daos_agent/daos_agent.sock, errno=2(No such file or directory)
mgmt ERR  src/mgmt/cli_mgmt.c:222 get_attach_info() failed to connect to /var/run/daos_agent/daos_agent.sock DER_MISC(-1025): 'Miscellaneous error'
failed to initialize daos: Miscellaneous error (-1025)


# Work around to check for daos_agent certification and start daos_agent
	#check for /etc/daos/certs/daosCA.crt, agent.crt and agent.key
	$ sudo systemctl enable daos_agent.service
	$ sudo systemctl start daos_agent.service
	$ sudo systemctl status daos_agent.service
```
### use daos command with invalid or wrong parameters
```
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
### dmg pool create failed due to no space
```
$ dmg pool create --size=50G mypool
Creating DAOS pool with automatic storage allocation: 50 GB NVMe + 6.00% SCM
ERROR: dmg: pool create failed: DER_NOSPACE(-1007): No space on storage target

# Workaround: dmg storage query scan to find current available storage
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
```
### dmg pool destroy force
```
# dmg pool destroy Timeout or failed due to pool having active connections
# Workaround using pool destroy --force option

	$ dmg pool destroy mypool --force
	Pool-destroy command succeeded
```
### dmg pool destroy recursive
```
# dmg pool destroy Timeout or failed due to pool having associated container(s)
# Workaround using pool destroy --recursive option

	$ dmg pool destroy mypool --recursive
	Pool-destroy command succeeded
```
### daos_engine fails to start with error "Address already in use"
```
09/26-15:25:38.06 node-1 DAOS[3851384/-1/0] external ERR  # [4462751.071824] mercury->cls: [error] /builddir/build/BUILD/mercury-2.2.0/src/na/na_ofi.c:3638
na_ofi_basic_ep_open(): fi_enable() failed, rc: -98 (Address already in use)

"Address already in use" will be observed when there is more than one engine per node sharing the same NIC and fabric_iface_port of engines are too close; e.g., the difference is not larger than 3.
libfabric (tcp and sockets providers) uses the port assigned by fabric_iface_port and the three following ports too. We need to make fabric_iface_port of engines on the same NIC distant enough to avoid such errors. Example:
# engine 0
fabric_iface_port: 31316
# engine 1
fabric_iface_port: 31416
```
### daos_agent cache of engine URIs is stale

The `daos_agent` cache may become invalid if `daos_engine` processes restart with different
configurations or IP addresses, or if the DAOS system is reformatted.
If this happens, the `daos` tool (as well as other I/O or `libdaos` operations) may return
`-DER_BAD_TARGET` (-1035) errors.

To resolve the issue, a privileged user may send a `SIGUSR2` signal to the `daos_agent` process to
force an immediate cache refresh.

## Diagnostic and Recovery Tools

!!! WARNING : Please be careful and use this tool under supervision of DAOS support team.

In case of PMEM device restored to healthy state, the ext4 filesystem
created on each PMEM devices may need to verified and repaired if needed.

!!! Make sure that PMEM is not in use and not mounted while doing check or repair.

#### Use dmg command to stop the daos engines.

```
# dmg -o test_daos_dmg.yaml system stop
Rank  Operation Result
----  --------- ------
[0-1] stop      OK
```
#### Stop the daos server service.

        #systemctl stop daos_server.service

#### Unmount the DAOS PMem mountpoint.

```
# df
Filesystem                    1K-blocks       Used  Available Use% Mounted on
devtmpfs                           4096          0       4096   0% /dev
tmpfs                          97577660          0   97577660   0% /dev/shm
tmpfs                          39031068      10588   39020480   1% /run
tmpfs                              4096          0       4096   0% /sys/fs/cgroup
/dev/sda5                      38141328    6117252   30056872  17% /
/dev/sda7                      28513060      45128   26996496   1% /var/tmp
wolf-1:/export/home/samirrav 6289369088 5927896416  361472672  95% /home/samirrav
/dev/pmem1                   3107622576     131116 3107475076   1% /mnt/daos1
/dev/pmem0                   3107622576     131172 3107475020   1% /mnt/daos0
# umount /mnt/daos*
# df
Filesystem                    1K-blocks       Used Available Use% Mounted on
devtmpfs                           4096          0      4096   0% /dev
tmpfs                          97577664          0  97577664   0% /dev/shm
tmpfs                          39031068      10588  39020480   1% /run
tmpfs                              4096          0      4096   0% /sys/fs/cgroup
/dev/sda5                      38141328    6120656  30053468  17% /
/dev/sda7                      28513060      45124  26996500   1% /var/tmp
wolf-1:/export/home/samirrav 6289369088 5927917792 361451296  95% /home/samirrav
#
```
### e2fsck


#### e2fsck command execution on non-corrupted file system.

- "-f": Force check file system even it seems clean.
- "-n": Use the option to assume an answer of 'no' to all questions.
```
#/sbin/e2fsck -f -n /dev/pmem1
e2fsck 1.43.8 (1-Jan-2018)
Pass 1: Checking inodes, blocks, and sizes
Pass 2: Checking directory structure
Pass 3: Checking directory connectivity
Pass 4: Checking reference counts
Pass 5: Checking group summary information
daos: 34/759040 files (0.0% non-contiguous), 8179728/777240064 blocks

#echo $?
0
```
- Return Code: "0 - No errors"

#### e2fsck command execution on corrupted file system.

- "-f": Force check file system even it seems clean.
- "-n": Use the option to assume an answer of 'no' to all questions.
- "-C0": To monitored the progress of the filesystem check.
```
# /sbin/e2fsck -f -n -C0 /dev/pmem1
e2fsck 1.43.8 (1-Jan-2018)
ext2fs_check_desc: Corrupt group descriptor: bad block for block bitmap
/sbin/e2fsck: Group descriptors look bad... trying backup blocks...
Pass 1: Checking inodes, blocks, and sizes
Pass 2: Checking directory structure
Pass 3: Checking directory connectivity
Pass 4: Checking reference counts
Pass 5: Checking group summary information
Block bitmap differences:  +(0--563) +(24092--24187) +(32768--33139) +(48184--48279) +(71904--334053) +334336 +335360
Fix? no

Free blocks count wrong for group #0 (32108, counted=32768).
Fix? no

Free blocks count wrong for group #1 (32300, counted=32768).
Fix? no
.....
.....

Free inodes count wrong for group #23719 (0, counted=32).
Fix? no

Padding at end of inode bitmap is not set. Fix? no


daos: ********** WARNING: Filesystem still has errors **********

daos: 13/759040 files (0.0% non-contiguous), 334428/777240064 blocks

# echo $?
4
```
- Return Code: "4 - File system errors left uncorrected"

#### e2fsck command to repair and fixing the issue.

- "-f": Force check file system even it seems clean.
- "-p": Automatically fix any filesystem problems that can be safely fixed without human intervention.
- "-C0": To monitored the progress of the filesystem check.
```
#/sbin/e2fsck -f -p -C0 /dev/pmem1
daos was not cleanly unmounted, check forced.
daos: Relocating group 96's block bitmap to 468...
daos: Relocating group 96's inode bitmap to 469...
daos: Relocating group 96's inode table to 470...
.....
.....
.....
daos: Relocating group 23719's inode table to 775946359...
Restarting e2fsck from the beginning...
daos: Padding at end of inode bitmap is not set. FIXED.
daos: 13/759040 files (0.0% non-contiguous), 334428/777240064 blocks

# echo $?
1
```
- Return Code: "1 - File system errors corrected"


### ipmctl

IPMCTL utility is used for Intel® Optane™ persistent memory for managing, diagnostic and testing purpose.
[IPMCTL user guide](https://docs.pmem.io/ipmctl-user-guide/) has more details about the utility.

DAOS user can use the [diagnostic](https://docs.pmem.io/ipmctl-user-guide/debug/run-diagnostic) and
[show error log](https://docs.pmem.io/ipmctl-user-guide/debug/show-error-log) functionality to debug the PMem related issues.

#### ipmctl show command to get the DIMM ID connected to specific CPU.

Example shows Eight PMem DIMMs are connected to both CPU0 and CPU1.

```
# ipmctl show -topology
 DimmID | MemoryType                  | Capacity    | PhysicalID| DeviceLocator
================================================================================
 0x0001 | Logical Non-Volatile Device | 507.688 GiB | 0x003a    | CPU0_DIMM_A2
 0x0011 | Logical Non-Volatile Device | 507.688 GiB | 0x003c    | CPU0_DIMM_B2
 0x0101 | Logical Non-Volatile Device | 507.688 GiB | 0x003e    | CPU0_DIMM_C2
 0x0111 | Logical Non-Volatile Device | 507.688 GiB | 0x0040    | CPU0_DIMM_D2
 0x0201 | Logical Non-Volatile Device | 507.688 GiB | 0x0042    | CPU0_DIMM_E2
 0x0211 | Logical Non-Volatile Device | 507.688 GiB | 0x0044    | CPU0_DIMM_F2
 0x0301 | Logical Non-Volatile Device | 507.688 GiB | 0x0046    | CPU0_DIMM_G2
 0x0311 | Logical Non-Volatile Device | 507.688 GiB | 0x0048    | CPU0_DIMM_H2
 0x1001 | Logical Non-Volatile Device | 507.688 GiB | 0x004a    | CPU1_DIMM_A2
 0x1011 | Logical Non-Volatile Device | 507.688 GiB | 0x004c    | CPU1_DIMM_B2
 0x1101 | Logical Non-Volatile Device | 507.688 GiB | 0x004e    | CPU1_DIMM_C2
 0x1111 | Logical Non-Volatile Device | 507.688 GiB | 0x0050    | CPU1_DIMM_D2
 0x1201 | Logical Non-Volatile Device | 507.688 GiB | 0x0052    | CPU1_DIMM_E2
 0x1211 | Logical Non-Volatile Device | 507.688 GiB | 0x0054    | CPU1_DIMM_F2
 0x1301 | Logical Non-Volatile Device | 507.688 GiB | 0x0056    | CPU1_DIMM_G2
 0x1311 | Logical Non-Volatile Device | 507.688 GiB | 0x0058    | CPU1_DIMM_H2
```

#### Run a quick diagnostic test on clean system

This test will verify the PMem health parameters are under acceptable values. It will return the single State indication
('OK', 'Warning', 'Failed', 'Aborted') based on health information from all the PMem modules.
```
#ipmctl start -diagnostic
```

#### Run quick diagnostic test on specific dimm from socket. By default it will run diagnostic test for all dimms.

* -dimm : DIMM ID from the ipmctl command above
```
#ipmctl start -diagnostic quick -dimm 0x0001
--Test = Quick
   State = Ok
   Message = The quick health check succeeded.
   --SubTest = Manageability
          State = Ok
   --SubTest = Boot status
          State = Ok
   --SubTest = Health
          State = Ok
```

#### Run quick diagnostic test on system which has health warning.

```
#ipmctl start -diagnostic quick
--Test = Quick
   State = Warning
   --SubTest = Manageability
          State = Ok
   --SubTest = Boot status
          State = Ok
   --SubTest = Health
          State = Warning
          Message.1 = The quick health check detected that PMem module 0x0001 is reporting a bad health state Noncritical failure (Package Sparing occurred).
          Message.2 = The quick health check detected that PMem module 0x0001 is reporting that it has no package spares available.
```

#### Run quick diagnostic test on system where PMem life remaining percentage is below threshold.

```
#ipmctl start -diagnostic quick
--Test = Quick
   State = Warning
   --SubTest = Manageability
          State = Ok
   --SubTest = Boot status
          State = Ok
   --SubTest = Health
          State = Warning
          Message.1 = The quick health check detected that PMem module 0x0001 is reporting percentage remaining at 10% which is less than the alarm threshold 50%
```

#### Run showerror Thermal and Media log on clean system.

```
#ipmctl show -error Thermal
No errors found on PMem module 0x0001
No errors found on PMem module 0x0011
No errors found on PMem module 0x0021
No errors found on PMem module 0x0101
No errors found on PMem module 0x0111
No errors found on PMem module 0x0121
No errors found on PMem module 0x1001
No errors found on PMem module 0x1011
No errors found on PMem module 0x1021
No errors found on PMem module 0x1101
No errors found on PMem module 0x1111
No errors found on PMem module 0x1121
Show Error executed successfully

#ipmctl show -error Media
No errors found on PMem module 0x0001
No errors found on PMem module 0x0011
No errors found on PMem module 0x0021
No errors found on PMem module 0x0101
No errors found on PMem module 0x0111
No errors found on PMem module 0x0121
No errors found on PMem module 0x1001
No errors found on PMem module 0x1011
No errors found on PMem module 0x1021
No errors found on PMem module 0x1101
No errors found on PMem module 0x1111
No errors found on PMem module 0x1121
Show Error executed successfully
```

#### Run showerror command for Thermal and Media log on non clean system.

```
#ipmctl show -error Thermal  Level=Low
 DimmID | System Timestamp    | Temperature | Reported
=============================================================
 0x0001 | 02/02/2022 21:45:26 | 86          | User Alarm Trip
 0x0001 | 02/03/2022 00:06:36 | 86          | User Alarm Trip
No errors found on PMem module 0x0011
No errors found on PMem module 0x0021
No errors found on PMem module 0x0101
No errors found on PMem module 0x0111
No errors found on PMem module 0x0121
No errors found on PMem module 0x1001
No errors found on PMem module 0x1011
No errors found on PMem module 0x1021
No errors found on PMem module 0x1101
No errors found on PMem module 0x1111
No errors found on PMem module 0x1121

# ipmctl show -error Media
 DimmID | System Timestamp    | Error Type
=============================================================
 0x0001 | 01/12/2022 21:18:53 | 0x04 - Locked/Illegal Access
 0x0001 | 01/12/2022 21:18:53 | 0x04 - Locked/Illegal Access
 ....
 ....
 ....
 0x1121 | 02/03/2022 16:50:17 | 0x04 - Locked/Illegal Access
 0x1121 | 02/03/2022 16:50:17 | 0x04 - Locked/Illegal Access
Show Error executed successfully
```
### ndctl

NDCTL is another utility library for managing the PMem. The ndctl provides functional used for PMem and namespace management,
device list, update firmware and more.
It can detect the media errors and scrub it to avoid accesses that could lead to uncorrectable memory error handling events.
[NDCTL user guide](https://docs.pmem.io/ndctl-user-guide/) has more details about the utility.
This utility can be used after ipmctl where name space is already created by ipmctl.

#### Read bad blocks data from sys filesystem on clean system.

```
# cat /sys/block/pmem*/badblocks
#
```
#### ndctl list command on clean system.

Please refer the [ndctl-list](https://docs.pmem.io/ndctl-user-guide/ndctl-man-pages/ndctl-list) command guide for more details about the command options.

Total Sixteen PMem connected to single system. Eight PMem DIMMs are connected to single socket (reference "ipmctl show -topology" section under ipmctl). 
The SCM modules are typically configured in AppDirect interleaved mode. They are thus presented to the operating system as a single PMem namespace per socket (in fsdax mode).

* -M : Include Media Error
```
# ndctl list -M
[
  {
        "dev":"namespace1.0",
        "mode":"fsdax",
        "map":"dev",
        "size":3183575302144,
        "uuid":"c0d02d75-2629-4393-ae91-44e914c82e7d",
        "sector_size":512,
        "align":2097152,
        "blockdev":"pmem1",
  },
  {
        "dev":"namespace0.0",
        "mode":"fsdax",
        "map":"dev",
        "size":3183575302144,
        "uuid":"7294d1c0-2145-436b-a2af-3115db839832",
        "sector_size":512,
        "align":2097152,
        "blockdev":"pmem0"
  }
]
```

#### ndctl start-scrub command ran on system which has bad blocks.

Please refer the [ndctl start-scrub](https://docs.pmem.io/ndctl-user-guide/ndctl-man-pages/ndctl-start-scrub) command guide for more information about the command options.

* Address Range Scrub is a device-specific method defined in the ACPI specification. Privileged software can call such as ARS at runtime to retrieve or scan for the locations of uncorrectable memory errors for all persistent memory in the platform.
Persistent memory uncorrectable errors are persistent. Unlike volatile memory, if power is lost or an application crashes and restarts, the uncorrectable error will remain on the hardware.
This can lead to an application getting stuck in an infinite loop on IO operation or it might get crash.

* Ideally to use this scrub check when system is recovered from PMem error or when PMem was replace and it need to check for any uncorrectable errors.

**Scrubbing takes time and this command will take longer time (From minutes to hours) based on number of PMem on the system and capacity.**

```
# ndctl start-scrub
[
  {
	"provider":"ACPI.NFIT",
	"dev":"ndbus0",
	"scrub_state":"active",
	"firmware":{
	  "activate_method":"reset"
	}
  }
]
```    

#### ndctl wait-scrub command ran on system which has bad blocks.

Please refer the [ndctl wait-scrub](https://docs.pmem.io/ndctl-user-guide/ndctl-man-pages/untitled-2) command guide for more information about the command options.
```
# ndctl wait-scrub
[
  {
	"provider":"ACPI.NFIT",
	"dev":"ndbus0",
	"scrub_state":"idle",
	"firmware":{
	  "activate_method":"reset"
	}
  }
]
```

#### Read bad blocks data from sys filesystem after scrubbing is finished.

```
# cat /sys/block/pmem*/badblocks
42 8
```

#### command execution on system where bad blocks are scrubbed.

Please refer the [ndctl list](https://docs.pmem.io/ndctl-user-guide/ndctl-man-pages/ndctl-list) user guide for more information about the command options.

* -M : Include Media Error

```
# ndctl list -M
[
  {
	"dev":"namespace1.0",
	"mode":"fsdax",
	"map":"dev",
	"size":3183575302144,
	"uuid":"c0d02d75-2629-4393-ae91-44e914c82e7d",
	"sector_size":512,
	"align":2097152,
	"blockdev":"pmem1",
	"badblock_count":8,
	"badblocks":[
	  {
		"offset":42,
		"length":8,
		"dimms":[
		  "nmem8",
		  "nmem10"
		]
	  }
	]
  },
  {
	"dev":"namespace0.0",
	"mode":"fsdax",
	"map":"dev",
	"size":3183575302144,
	"uuid":"7294d1c0-2145-436b-a2af-3115db839832",
	"sector_size":512,
	"align":2097152,
	"blockdev":"pmem0"
  }
]
```
### pmempool

The pmempool is a management tool for Persistent Memory pool files created by PMDK libraries.
DAOS uses the PMDK library to manage persistence inside ext4 files.
[pmempool](https://pmem.io/pmdk/manpages/linux/v1.9/pmempool/pmempool-check.1/) can check consistency of a given pool file.
It can be run with -r (repair) option which can fix some of the issues with pool file. DAOS will have more number of such pool file (vos-*), based
on number of targets mention per daos engine. User may need to check each vos pool file for corruption on faulty pool.

#### Unclean shutdown

Example of the system which is not shutdown properly and that set the mode as dirty on the VOS pool file.

*  -v: More verbose.
```
# pmempool check /mnt/daos0/0d977cd9-2571-49e8-902d-953f6adc6120/vos-0  -v
checking shutdown state
shutdown state is dirty
/mnt/daos0/0d977cd9-2571-49e8-902d-953f6adc6120/vos-0: not consistent
# echo $?
1
```

#### Repair command

Example of check repair command ran on the system to fix the unclean shutdown.

*  -v: More verbose.
*  -r: repair the pool.
*  -y: Answer yes to all question.
```
# pmempool check /mnt/daos0/894b94ee-cdb2-4241-943c-08769542d327/vos-0 -vry
checking shutdown state
shutdown state is dirty
resetting pool_hdr.sds
checking pool header
pool header correct
/mnt/daos0/894b94ee-cdb2-4241-943c-08769542d327/vos-0: repaired
# echo $?
0
```

#### Check consistency.

Check the consistency of the VOS pool file after repair.
```
# pmempool check /mnt/daos0/894b94ee-cdb2-4241-943c-08769542d327/vos-0 -v
checking shutdown state
shutdown state correct
checking pool header
pool header correct
/mnt/daos0/894b94ee-cdb2-4241-943c-08769542d327/vos-0: consistent
# echo $?
0
```

## Syslog

[`RAS events`](https://docs.daos.io/v2.6/admin/administration/#ras-events) are printed to the Syslog
by 'daos_server' processes via the Go standard library API.
If no Syslog daemon is configured on the host, errors will be printed to the 'daos_server' log file:

```
ERROR: failed to create syslogger with priority 30 (severity=ERROR, facility=DAEMON): Unix syslog delivery error
```

These errors will not prevent DAOS from running as expected but indicate that no Syslog messages
will be delivered.

'rsyslog' is the Syslog daemon shipped with most operating systems and to check if the service is
running under systemd run the following command:

```
# sudo systemctl status rsyslog
● rsyslog.service - System Logging Service
   Loaded: loaded (/usr/lib/systemd/system/rsyslog.service; enabled; vendor preset: enabled)
   Active: active (running) since Mon 2022-05-23 16:12:31 UTC; 1 weeks 1 days ago
     Docs: man:rsyslogd(8)
           https://www.rsyslog.com/doc/
 Main PID: 1962 (rsyslogd)
    Tasks: 3 (limit: 1648282)
   Memory: 5.0M
   CGroup: /system.slice/rsyslog.service
           └─1962 /usr/sbin/rsyslogd -n

May 23 16:12:31 wolf-164.wolf.hpdd.intel.com systemd[1]: Starting System Logging Service...
May 23 16:12:31 wolf-164.wolf.hpdd.intel.com rsyslogd[1962]: [origin software="rsyslogd" swVersion="8.21>
May 23 16:12:31 wolf-164.wolf.hpdd.intel.com systemd[1]: Started System Logging Service.
May 23 16:12:31 wolf-164.wolf.hpdd.intel.com rsyslogd[1962]: imjournal: journal files changed, reloading>
May 29 03:18:01 wolf-164.wolf.hpdd.intel.com rsyslogd[1962]: [origin software="rsyslogd" swVersion="8.21>
```

To configure a Syslog daemon to resolve the delivery errors and receive messages from 'daos_server'
consult the relevant operating system specific documentation for installing and/or enabling a syslog
server package e.g. 'rsyslog'.

## Tools to debug connectivity issues across nodes

### ifconfig
```
$ ifconfig
lo: flags=73<UP,LOOPBACK,RUNNING>  mtu 65536
        inet 127.0.0.1  netmask 255.0.0.0
        inet6 ::1  prefixlen 128  scopeid 0x10<host>
        loop  txqueuelen 1000  (Local Loopback)
        RX packets 127  bytes 9664 (9.4 KiB)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 127  bytes 9664 (9.4 KiB)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0
eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 9000
        inet 10.165.192.121  netmask 255.255.255.128  broadcast 10.165.192.127
        inet6 fe80::9a03:9bff:fea2:9716  prefixlen 64  scopeid 0x20<link>
        ether 98:03:9b:a2:97:16  txqueuelen 1000  (Ethernet)
        RX packets 2347  bytes 766600 (748.6 KiB)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 61  bytes 4156 (4.0 KiB)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0
eth1: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 9000
        inet 10.165.192.122  netmask 255.255.255.128  broadcast 10.165.192.127
        inet6 fe80::9a03:9bff:fea2:967e  prefixlen 64  scopeid 0x20<link>
        ether 98:03:9b:a2:96:7e  txqueuelen 1000  (Ethernet)
        RX packets 2346  bytes 766272 (748.3 KiB)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 61  bytes 4156 (4.0 KiB)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0
```
You can get the ip and network interface card (NIC) name with ifconfig. Important: Please run ifconfig on both DAOS server and client nodes to make sure mtu size are same for the network interfaces on different nodes. Mismatched mtu size could lead to DAOS hang on RDMA over converged Ethernet (RoCE) interfaces.

### lstopo-no-graphics
```
$ lstopo-no-graphics
...
    HostBridge
      PCIBridge
        PCI 18:00.0 (Ethernet)
          Net "eth0"
          OpenFabrics "mlx5_0"
...
    HostBridge
      PCIBridge
        PCI af:00.0 (Ethernet)
          Net "eth1"
          OpenFabrics "mlx5_1"
...
```
You can get the domain name and numa node information of your NICs.
In case lstopo-no-graphics in not installed, you can install package "hwloc" with yum/dnf or other package managers.

### ping
```
client_node $ ping -c 3 -I eth1 10.165.192.121
PING 10.165.192.121 (10.165.192.121) from 10.165.192.2 ens102: 56(84) bytes of data.
64 bytes from 10.165.192.121: icmp_seq=1 ttl=64 time=0.177 ms
64 bytes from 10.165.192.121: icmp_seq=2 ttl=64 time=0.120 ms
64 bytes from 10.165.192.121: icmp_seq=3 ttl=64 time=0.083 ms
```
Make sure ping can reach the NIC your DAOS server is bound to.

### fi_pingpong
```
server_node $ fi_pingpong -p "tcp;ofi_rxm" -e rdm -d eth0
client_node $ fi_pingpong -p "tcp;ofi_rxm" -e rdm -d eth0 ip_of_eth0_server

bytes   #sent   #ack     total       time     MB/sec    usec/xfer   Mxfers/sec
64      10      =10      1.2k        0.03s      0.05    1378.30       0.00
256     10      =10      5k          0.00s     22.26      11.50       0.09
1k      10      =10      20k         0.00s     89.04      11.50       0.09
4k      10      =10      80k         0.00s    320.00      12.80       0.08
64k     10      =10      1.2m        0.01s    154.89     423.10       0.00
1m      10      =10      20m         0.01s   2659.00     394.35       0.00

Make sure communications with tcp can go through.

server_node $ fi_pingpong -p "tcp;ofi_rxm" -e rdm -d eth0
client_node $ fi_pingpong -p "tcp;ofi_rxm" -e rdm -d eth0 ip_of_eth0_server

Make sure communications with verbs can go through.

server_node $ fi_pingpong -p "verbs;ofi_rxm" -e rdm -d mlx5_0
client_node $ fi_pingpong -p "verbs;ofi_rxm" -e rdm -d mlx5_0 ip_of_mlx5_0_server
```
### ib_send_lat
```
server_node $ ib_send_lat -d mlx5_0 -s 16384 -D 3
client_node $ ib_send_lat -d mlx5_0 -s 16384 -D 3 ip_of_server
```
This test checks whether verbs goes through with Infiniband or RoCE cards. In case ib_send_lat in not installed, you can install package "perftest" with yum/dnf or other package managers.

## Tools to measure the network latency and bandwidth across nodes

### The tools in perftest for Infiniband and RoCE
You can install package "perftest" with yum/dnf or other package managers if it is not available.

Examples for measuring bandwidth,
```
ib_read_bw -a
ib_read_bw -a 192.168.1.46

ib_write_bw -a
ib_write_bw -a 192.168.1.46

ib_send_bw -a
ib_send_bw -a 192.168.1.46
```
Examples for measuring latency,
```
ib_read_lat -a
ib_read_lat -a 192.168.1.46

ib_write_lat -a
ib_write_lat -a 192.168.1.46

ib_send_lat -a
ib_send_lat -a 192.168.1.46
```

### fi_pingpong for Ethernet
You can install package "libfabric" with yum/dnf or other package managers if it is not available.

Example,
```
server_node $ fi_pingpong -p "tcp;ofi_rxm" -e rdm -d eth0 -I 1000
client_node $ fi_pingpong -p "tcp;ofi_rxm" -e rdm -d eth0 -I 1000 ip_of_eth0_server
```
This reports network bandwidth. One can deduce the latency for given packet size.

## Tools to diagnose network issues for a large cluster

### [Intel CLuster Checker](https://www.intel.com/content/www/us/en/developer/tools/oneapi/cluster-checker.html)
This suite contains multiple useful tools including network_time_uniformity to debug network issue.

### [mpi-benchmarks](https://github.com/intel/mpi-benchmarks)
Tools like IMB-P2P, IMB-MPI1, and IMB-RMA are helpful for the sanity check of the latency and bandwidth.
```
$ for((i=1;i<=65536;i*=4)); do echo "$i"; done &> msglen
$ mpirun -np 4 -f hostlist ./IMB-P2P -msglen msglen PingPong
#----------------------------------------------------------------
# Benchmarking PingPong
# #processes = 4
#----------------------------------------------------------------
       #bytes #repetitions      t[usec]   Mbytes/sec      Msg/sec
            1       100000        24.50         0.08        81627
            4       100000        24.50         0.33        81631
           16       100000        24.50         1.31        81629
           64       100000        24.50         5.22        81631
          256       100000        24.60        20.73        80983
         1024       100000        49.50        41.37        40404
         4096       100000       224.05        36.43         8894
        16384        51200       230.22       141.65         8646
        65536        12800       741.47       176.58         2694
```

## Bug Report

Bugs should be reported through our [issue tracker](https://jira.daos.io/)
with a test case to reproduce the issue (when applicable) and debug logs.

After creating a ticket, logs should be gathered from the locations
described in the [Log Files](#log-files) section of this document and
attached to the ticket.

To avoid problems with attaching large files, please attach the logs
in a compressed container format, such as .zip or .tar.bz2.

