# DAOS Client Telemetry

The DAOS client library (libdaos) includes performance monitoring and workload characterization
capabilities provided via the <a href="/src/gurt/telemetry.md">DAOS telemetry library</a>. This
document was written to describe those capabilities and provide a starting point for site-specific
integration possibilities.

## Unmanaged Use Cases

The simplest scenarios supported by client telemetry allow a developer or advanced user to dump a
final snapshot of the client telemetry at process exit. These modes do not require any special
configuration of the DAOS agent or external monitoring infrastructure. The tradeoff for simplicity
is the loss of information about how the metric values changed over time. Some complex metric types
(stats gauges and histograms) do provide extra information derived over time, but the final dump
still only contains a single datapoint for those extra metric values.

### Manual Metrics Dump via Environment Variable

The header file <a href="/src/include/daos/metrics.h">&lt;daos/metrics.h&gt;</a> defines a number of
environment variables that may be set in the client process environment to control the client
telemetry library. The only one directly relevant to this use case, however, is
`DAOS_CLIENT_METRICS_DUMP_DIR`, which may be set to a local or shared directory. When the client
process exits, it will write out a
<a href="https://en.wikipedia.org/wiki/Comma-separated_values">CSV</a>-formatted file that contains
one line for each metric. See the <a href="#client-telemetry-examples">Examples</a> section below for more
information. Note that in this mode, the client telemetry is recorded in non-shared memory segments,
so it is not possible to both dump to CSV and sample the telemetry periodically during the client
process run time. The telemetry recorded in this mode also contains the full set of client metrics,
as the library is initialized via the client invocation of `daos_init()`.

### Automatic Metrics Dump per Container

This mode allows a user with write access to a container to enable metrics dump for any libdaos
client that accesses the container. In addition to the previously-described mode where the CSV
files are written out to a directory on a local or shared filesystem, this mode also provides a
capability to specify that the CSVs should be written to a DAOS container. Using a DAOS container
for the metrics storage simplifies use cases where metrics may be collected for many processes
distributed over a large set of client machines. In this mode, a client process will dump metrics
directly to the metrics container using the DFS API, without any need to set up a mountpoint
for the container.

Enabling this mode requires use of the `daos` tool to set the relevant parameters for a container.
In its simplest form, the only required parameters are the source pool/container, and the destination
container in the same pool. The source pool/container may be obtained from a dfuse mountpoint via the
`--path` parameter, or may be specified as positional parameters.

> **_NOTE:_** The destination container must exist, must use the POSIX layout option, and must be
writable by the user under which the client process is running. Future updates may allow the
destination container to be created automatically.

> **_NOTE:_** If telemetry is to be enabled for Interception Library (IL) clients _after_ dfuse has
been started, then the `--path` parameter must be used in order to toggle the setting in the
running dfuse process.
Due to limitations of the current implementation, the command needs to be run on all client machines
running a `dfuse` process. This limitation may be avoided by enabling telemetry for a container
_before_ starting any dfuse clients.

```shell
# Dump metrics to a container in the same pool as the source container:
$ daos container telemetry enable poolA contA --dump-cont=contB

# Dump metrics to a container in a different pool:
$ daos container telemetry enable poolA contA --dump-pool=poolB --dump-cont=contB

# Obtain the source pool/cont from a dfuse mountpoint:
$ daos container telemetry enable --path=/mnt/daos --dump-cont=contB
```

To disable telemetry for a container, use the disable command (**NB**: the same
consideration for a running dfuse process applies):

```shell
# Disable telemetry for a container without running dfuse clients:
$ daos container telemetry disable poolA contA

# Disable telemetry for a container backing a running dfuse client:
$ daos container telemetry disable --path=/mnt/daos
```

> **_NOTE:_** The CSV files for clients configured to dump their telemetry using this
method may only contain a subset of available client metrics. The difference in
behavior depends on whether or not client telemetry has been enabled via `daos_agent`
configuration. In cases where client telemetry is _only_ enabled via container
parameters, the set of client metrics will be restricted to the DFS-layer metrics for
the container where telemetry is enabled.

## Managed Use Cases

In addition to the user-driven use cases, the client telemetry feature provides capabilities
for integration with a site-provided telemetry solution. For a full description of the
Prometheus integration, refer to the <a href="/src/gurt/telemetry.md">DAOS telemetry library</a> documentation.

### Enabling Client Telemetry for All libdaos Clients

In this mode, libdaos clients will record telemetry in shared memory segments so that the
`daos_agent` process can export the metrics via the configured Prometheus endpoint. By default,
these shared memory segments are cleaned up immediately after the client process exits, but
the agent may be configured to retain the segments for some period of time after client exit.

The minimal <a href="/utils/config/daos_agent.yml">daos_agent.yml</a> configuration required to
enable client telemetry would look something like the following:

```yaml
telemetry_port: 9192 # set to a port that will otherwise be unused on the client machine(s)
telemetry_enabled: true
```

If it is desirable to retain the client telemetry segments for a period of time after client exit,
use the `telemetry_retain:` parameter to set a duration value (e.g. 30s, 1m). This parameter may
be used to allow the monitoring infrastructure time to capture a final sample. Future updates to
the implementation may provide other retention options, e.g. "retain until read".

### Enabling Client Telemetry for Selected libdaos Clients

In scenarios where some clients should use shared memory for sampled telemetry and other clients
should be controlled by user-initiated action, it is possible to configure the agent such that
it only enables telemetry for a subset of clients. One such example of this scenario might be an
environment where long-running `dfuse` clients are monitored via telemetry infrastructure, and
other ephemeral clients are optionally configured to dump telemetry on exit for post-workload
analysis.

The `daos_agent.yml` configuration parameters controlling this feature would look something like
the following (**NB**: These parameters are mutually exclusive):

```yaml
telemetry_enabled_procs: <regex> # only clients matching this regex will have telemetry enabled
telemetry_disabled_procs: <regex> # clients matching this regex will not have telemetry disabled
```

### Developer Access

When shared telemetry has been enabled for a client process, the typical use case for it is to
be exported via `daos_agent`. It is, however, possible for a user to access the shared telemetry
via the `daos_metrics` tool. This tool was created primarily to enable lightweight developer
access to the server and client telemetry via shared memory on the same host as the
telemetry producer process.

Running the tool requires read permissions on the shared memory segments exposed by the client
and knowledge of the client process PID, e.g. `daos_metrics --cli_pid=<pid>`

# Client Telemetry Examples

The following section contains examples of DAOS client telemetry dumped at process exit and
sampled during runtime. Note that these are only provided as examples and are not intended to
be canonical references for the entire set of available metrics, which may change from release
to release.

## CSV Dump Files

When the `DAOS_CLIENT_METRICS_DUMP_DIR` environment variable is set, the dump path will be whatever
was set, and the filename will follow the pattern `<dc_jobid>-<pid>.csv`. The value of `<dc_jobid>`
is defined in <a href="/src/client/api/job.c">job.c</a>. If the `DAOS_JOBID` environment
variable has been set, then that value will be used. Otherwise the default (`<hostname>-<pid>`) will
be used.

When the metrics container dump method is enabled, the path to each client CSV is generated following
a pattern that is designed to reduce storage "hot spots" and allow for easy time-based cleanup of
client CSVs. The generated paths differ slightly depending on whether or not a custom `DAOS_JOBID`
value has been set:

```
# DAOS_JOBID=foo
-> <root>/<yyyy>/<mm>/<dd>/<hh>/job/foo/<proc_name>/<epoch>-<hostname>-<pid>.csv
```

```
# DAOS_JOBID unset
-> <root>/<yyyy>/<mm>/<dd>/<hh>/proc/<proc_name>/<epoch>-<hostname>-<pid>.csv
```

Note that the time-based components of the path are always in GMT. Future updates may allow
for more configurability of the path pattern.

### CSV Dump File Example

```
$ cat /mnt/daos/metrics/2025/04/30/18/proc/rsync/1746038198-testhost-2687361.csv
name,value,min,max,mean,sample_size,sum,std_dev
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/CHMOD,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/CHOWN,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/CREATE,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/GETSIZE,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/GETXATTR,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/LSXATTR,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/MKDIR,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/OPEN,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/OPENDIR,15
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/READ,34
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/READDIR,15
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/READLINK,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/RENAME,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/RMXATTR,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/SETATTR,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/SETXATTR,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/STAT,83
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/SYMLINK,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/SYNC,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/TRUNCATE,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/UNLINK,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/ops/WRITE,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes,39534,3426,39534,13925.411765,34,473464,7312.911418
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/0_255_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/256_511_bytes,3
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/512_1023_bytes,27
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/1024_2047_bytes,2
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/2048_4095_bytes,1
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/4096_8191_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/8192_16383_bytes,1
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/16384_32767_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/32768_65535_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/65536_131071_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/131072_262143_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/262144_524287_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/524288_1048575_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/1048576_2097151_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/2097152_4194303_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/read_bytes/4194304_inf_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/0_255_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/256_511_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/512_1023_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/1024_2047_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/2048_4095_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/4096_8191_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/8192_16383_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/16384_32767_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/32768_65535_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/65536_131071_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/131072_262143_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/262144_524287_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/524288_1048575_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/1048576_2097151_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/2097152_4194303_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dfs/write_bytes/4194304_inf_bytes,0
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/mount_time,Wed Apr 30 18:36:36 2025
2687361/pool/fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd/container/1a6fef2b-0cbc-4e11-9edd-6abb252e93c8/dump_time,Wed Apr 30 18:36:38 2025
```

## Prometheus Export Example

The `dmg` tool includes a subcommand for displaying telemetry exposed via Prometheus endpoint. When
the `daos_agent` process has been configured to export client telemetry, the `dmg` tool may be used
to inspect the metrics. The metrics returned from a `dmg` invocation are from a single sample of
all available metrics at that time.

In this example, the output is from querying an agent that is managing telemetry for a single
`dfuse` process:

```
$ dmg telemetry metrics query -p 9292 --hostname localhost
connecting to localhost:9292...
- Metric Set: client_dfs_ops_chmod (Type: Counter)
  Count of CHMOD calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_chown (Type: Counter)
  Count of CHOWN calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_create (Type: Counter)
  Count of CREATE calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_getsize (Type: Counter)
  Count of GETSIZE calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_getxattr (Type: Counter)
  Count of GETXATTR calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_lsxattr (Type: Counter)
  Count of LSXATTR calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_mkdir (Type: Counter)
  Count of MKDIR calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_open (Type: Counter)
  Count of OPEN calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_opendir (Type: Counter)
  Count of OPENDIR calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_read (Type: Counter)
  Count of READ calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_readdir (Type: Counter)
  Count of READDIR calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_readlink (Type: Counter)
  Count of READLINK calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_rename (Type: Counter)
  Count of RENAME calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_rmxattr (Type: Counter)
  Count of RMXATTR calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_setattr (Type: Counter)
  Count of SETATTR calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_setxattr (Type: Counter)
  Count of SETXATTR calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_stat (Type: Counter)
  Count of STAT calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_symlink (Type: Counter)
  Count of SYMLINK calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_sync (Type: Counter)
  Count of SYNC calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_truncate (Type: Counter)
  Count of TRUNCATE calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_unlink (Type: Counter)
  Count of UNLINK calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_ops_write (Type: Counter)
  Count of WRITE calls
    Metric  Labels                                                                                                                Value 
    ------  ------                                                                                                                ----- 
    Counter (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_dfs_read_bytes (Type: Histogram)
  dfs read bytes
    Metric                      Labels                                                                                                                Value        
    ------                      ------                                                                                                                -----        
    Sample Count                (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Sample Sum                  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(0) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 255          
    Bucket(0) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(1) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 511          
    Bucket(1) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(2) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 1023         
    Bucket(2) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(3) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 2047         
    Bucket(3) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(4) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 4095         
    Bucket(4) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(5) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 8191         
    Bucket(5) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(6) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 16383        
    Bucket(6) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(7) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 32767        
    Bucket(7) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(8) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 65535        
    Bucket(8) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(9) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 131071       
    Bucket(9) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(10) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 262143       
    Bucket(10) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(11) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 524287       
    Bucket(11) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(12) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 1.048575e+06 
    Bucket(12) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(13) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 2.097151e+06 
    Bucket(13) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(14) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 4.194303e+06 
    Bucket(14) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(15) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) +Inf         
    Bucket(15) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            

- Metric Set: client_dfs_write_bytes (Type: Histogram)
  dfs write bytes
    Metric                      Labels                                                                                                                Value        
    ------                      ------                                                                                                                -----        
    Sample Count                (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Sample Sum                  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(0) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 255          
    Bucket(0) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(1) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 511          
    Bucket(1) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(2) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 1023         
    Bucket(2) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(3) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 2047         
    Bucket(3) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(4) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 4095         
    Bucket(4) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(5) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 8191         
    Bucket(5) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(6) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 16383        
    Bucket(6) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(7) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 32767        
    Bucket(7) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(8) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 65535        
    Bucket(8) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(9) Upper Bound       (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 131071       
    Bucket(9) Cumulative Count  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(10) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 262143       
    Bucket(10) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(11) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 524287       
    Bucket(11) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(12) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 1.048575e+06 
    Bucket(12) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(13) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 2.097151e+06 
    Bucket(13) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(14) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 4.194303e+06 
    Bucket(14) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            
    Bucket(15) Upper Bound      (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) +Inf         
    Bucket(15) Cumulative Count (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0            

- Metric Set: client_io_latency_fetch (Type: Gauge)
  fetch RPC processing time
    Metric Labels                                                      Value 
    ------ ------                                                      ----- 
    Gauge  (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  3060  
    Gauge  (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_fetch_max (Type: Gauge)
  fetch RPC processing time (max value)
    Metric Labels                                                      Value 
    ------ ------                                                      ----- 
    Gauge  (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  3227  
    Gauge  (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_fetch_mean (Type: Gauge)
  fetch RPC processing time (mean)
    Metric Labels                                                      Value 
    ------ ------                                                      ----- 
    Gauge  (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  2121  
    Gauge  (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_fetch_min (Type: Gauge)
  fetch RPC processing time (min value)
    Metric Labels                                                      Value 
    ------ ------                                                      ----- 
    Gauge  (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  76    
    Gauge  (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_fetch_samples (Type: Counter)
  fetch RPC processing time (samples)
    Metric  Labels                                                      Value 
    ------  ------                                                      ----- 
    Counter (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  3     
    Counter (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_fetch_stddev (Type: Gauge)
  fetch RPC processing time (std dev)
    Metric Labels                                                      Value              
    ------ ------                                                      -----              
    Gauge  (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0                  
    Gauge  (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0                  
    Gauge  (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0                  
    Gauge  (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0                  
    Gauge  (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0                  
    Gauge  (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0                  
    Gauge  (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0                  
    Gauge  (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0                  
    Gauge  (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0                  
    Gauge  (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0                  
    Gauge  (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0                  
    Gauge  (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  1772.9892836675579 
    Gauge  (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0                  
    Gauge  (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0                  
    Gauge  (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0                  
    Gauge  (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0                  

- Metric Set: client_io_latency_fetch_sum (Type: Counter)
  fetch RPC processing time (sum)
    Metric  Labels                                                      Value 
    ------  ------                                                      ----- 
    Counter (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  6363  
    Counter (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_fetch_sumsquares (Type: Counter)
  fetch RPC processing time (sum of squares)
    Metric  Labels                                                      Value         
    ------  ------                                                      -----         
    Counter (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0             
    Counter (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0             
    Counter (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0             
    Counter (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0             
    Counter (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0             
    Counter (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0             
    Counter (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0             
    Counter (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0             
    Counter (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0             
    Counter (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0             
    Counter (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0             
    Counter (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  1.9782905e+07 
    Counter (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0             
    Counter (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0             
    Counter (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0             
    Counter (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0             

- Metric Set: client_io_latency_update (Type: Gauge)
  update RPC processing time
    Metric Labels                                                      Value 
    ------ ------                                                      ----- 
    Gauge  (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_update_max (Type: Gauge)
  update RPC processing time (max value)
    Metric Labels                                                      Value 
    ------ ------                                                      ----- 
    Gauge  (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_update_mean (Type: Gauge)
  update RPC processing time (mean)
    Metric Labels                                                      Value 
    ------ ------                                                      ----- 
    Gauge  (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_update_min (Type: Gauge)
  update RPC processing time (min value)
    Metric Labels                                                      Value 
    ------ ------                                                      ----- 
    Gauge  (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_update_samples (Type: Counter)
  update RPC processing time (samples)
    Metric  Labels                                                      Value 
    ------  ------                                                      ----- 
    Counter (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_update_stddev (Type: Gauge)
  update RPC processing time (std dev)
    Metric Labels                                                      Value 
    ------ ------                                                      ----- 
    Gauge  (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Gauge  (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Gauge  (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Gauge  (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_update_sum (Type: Counter)
  update RPC processing time (sum)
    Metric  Labels                                                      Value 
    ------  ------                                                      ----- 
    Counter (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_latency_update_sumsquares (Type: Counter)
  update RPC processing time (sum of squares)
    Metric  Labels                                                      Value 
    ------  ------                                                      ----- 
    Counter (jobid=dfuse, pid=2688260, size=128KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=16KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=1KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=1MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=256B, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=256KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=2KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=2MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=32KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=4KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=4MB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=512B, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=512KB, tid=140048582007104) 0     
    Counter (jobid=dfuse, pid=2688260, size=64KB, tid=140048582007104)  0     
    Counter (jobid=dfuse, pid=2688260, size=8KB, tid=140048582007104)   0     
    Counter (jobid=dfuse, pid=2688260, size=GT4MB, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_enum_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_akey_punch_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_compound_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_enum_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_dkey_punch_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_agg_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_ec_rep_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_fetch_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_fetch_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 1     

- Metric Set: client_io_ops_fetch_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0.5   

- Metric Set: client_io_ops_fetch_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_fetch_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 6     

- Metric Set: client_io_ops_fetch_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value              
    ------ ------                                          -----              
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0.5477225575051661 

- Metric Set: client_io_ops_fetch_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 3     

- Metric Set: client_io_ops_fetch_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 3     

- Metric Set: client_io_ops_key2anchor_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key2anchor_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key_query_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key_query_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 1     

- Metric Set: client_io_ops_key_query_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0.5   

- Metric Set: client_io_ops_key_query_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key_query_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 2     

- Metric Set: client_io_ops_key_query_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value              
    ------ ------                                          -----              
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0.7071067811865476 

- Metric Set: client_io_ops_key_query_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 1     

- Metric Set: client_io_ops_key_query_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 1     

- Metric Set: client_io_ops_key_query_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 252   

- Metric Set: client_io_ops_key_query_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 252   

- Metric Set: client_io_ops_key_query_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 252   

- Metric Set: client_io_ops_key_query_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 252   

- Metric Set: client_io_ops_key_query_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 1     

- Metric Set: client_io_ops_key_query_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_key_query_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 252   

- Metric Set: client_io_ops_key_query_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 63504 

- Metric Set: client_io_ops_migrate_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_migrate_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_punch_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_coll_query_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_enum_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_punch_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_obj_sync_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_recx_enum_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_akey_punch_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_dkey_punch_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_latency (Type: Gauge)
  object RPC processing time
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_latency_max (Type: Gauge)
  object RPC processing time (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_latency_mean (Type: Gauge)
  object RPC processing time (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_latency_min (Type: Gauge)
  object RPC processing time (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_latency_samples (Type: Counter)
  object RPC processing time (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_latency_stddev (Type: Gauge)
  object RPC processing time (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_latency_sum (Type: Counter)
  object RPC processing time (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_punch_latency_sumsquares (Type: Counter)
  object RPC processing time (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_update_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_update_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_update_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_update_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_update_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_update_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_update_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_tgt_update_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_update_active (Type: Gauge)
  number of active object RPCs
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_update_active_max (Type: Gauge)
  number of active object RPCs (max value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_update_active_mean (Type: Gauge)
  number of active object RPCs (mean)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_update_active_min (Type: Gauge)
  number of active object RPCs (min value)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_update_active_samples (Type: Counter)
  number of active object RPCs (samples)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_update_active_stddev (Type: Gauge)
  number of active object RPCs (std dev)
    Metric Labels                                          Value 
    ------ ------                                          ----- 
    Gauge  (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_update_active_sum (Type: Counter)
  number of active object RPCs (sum)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_io_ops_update_active_sumsquares (Type: Counter)
  number of active object RPCs (sum of squares)
    Metric  Labels                                          Value 
    ------  ------                                          ----- 
    Counter (jobid=dfuse, pid=2688260, tid=140048582007104) 0     

- Metric Set: client_net_hg_active_rpcs (Type: Gauge)
  Mercury-layer count of active RPCs
    Metric Labels                                                  Value 
    ------ ------                                                  ----- 
    Gauge  (context=0, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     
    Gauge  (context=1, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     

- Metric Set: client_net_hg_bulks (Type: Counter)
  Mercury-layer count of bulk transfers
    Metric  Labels                                                  Value 
    ------  ------                                                  ----- 
    Counter (context=0, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     
    Counter (context=1, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     

- Metric Set: client_net_hg_extra_bulk_req (Type: Counter)
  Mercury-layer count of RPCs with extra bulk request
    Metric  Labels                                                  Value 
    ------  ------                                                  ----- 
    Counter (context=0, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     
    Counter (context=1, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     

- Metric Set: client_net_hg_extra_bulk_resp (Type: Counter)
  Mercury-layer count of RPCs with extra bulk response
    Metric  Labels                                                  Value 
    ------  ------                                                  ----- 
    Counter (context=0, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     
    Counter (context=1, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     

- Metric Set: client_net_hg_mr_copies (Type: Counter)
  Mercury-layer count of multi-recv RPC requests requiring a copy
    Metric  Labels                                                  Value 
    ------  ------                                                  ----- 
    Counter (context=0, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     
    Counter (context=1, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     

- Metric Set: client_net_hg_req_recv (Type: Counter)
  Mercury-layer count of RPC requests received
    Metric  Labels                                                  Value 
    ------  ------                                                  ----- 
    Counter (context=0, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     
    Counter (context=1, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     

- Metric Set: client_net_hg_req_sent (Type: Counter)
  Mercury-layer count of RPC requests sent
    Metric  Labels                                                  Value 
    ------  ------                                                  ----- 
    Counter (context=0, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     
    Counter (context=1, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     

- Metric Set: client_net_hg_resp_recv (Type: Counter)
  Mercury-layer count of RPC responses received
    Metric  Labels                                                  Value 
    ------  ------                                                  ----- 
    Counter (context=0, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     
    Counter (context=1, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     

- Metric Set: client_net_hg_resp_sent (Type: Counter)
  Mercury-layer count of RPC responses sent
    Metric  Labels                                                  Value 
    ------  ------                                                  ----- 
    Counter (context=0, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     
    Counter (context=1, jobid=dfuse, pid=2688260, provider=ofi+tcp) 0     

- Metric Set: client_pool_dump_time (Type: Gauge)
  container dump time
    Metric Labels                                                                                                                Value 
    ------ ------                                                                                                                ----- 
    Gauge  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ec_agg_blocked (Type: Counter)
  total number of EC agg pauses due to VOS discard or agg
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ec_update_full_stripe (Type: Counter)
  total number of EC full-stripe updates
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ec_update_partial (Type: Counter)
  total number of EC partial updates
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_mount_time (Type: Gauge)
  container mount time
    Metric Labels                                                                                                                Value           
    ------ ------                                                                                                                -----           
    Gauge  (container=1a6fef2b-0cbc-4e11-9edd-6abb252e93c8, jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 1.746038345e+09 

- Metric Set: client_pool_ops_akey_enum (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_akey_punch (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_compound (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_dkey_enum (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_dkey_punch (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_ec_agg (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_ec_rep (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_fetch (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_key2anchor (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_key_query (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_migrate (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_obj_coll_punch (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_obj_coll_query (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_obj_enum (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_obj_punch (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_obj_sync (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_recx_enum (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_tgt_akey_punch (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_tgt_dkey_punch (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_tgt_punch (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_tgt_update (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_ops_update (Type: Counter)
  total number of processed object RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_resent (Type: Counter)
  total number of resent update RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_restarted (Type: Counter)
  total number of restarted update ops
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_retry (Type: Counter)
  total number of retried update RPCs
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_pool_xferred_fetch (Type: Counter)
  total number of bytes fetched/read
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 956   

- Metric Set: client_pool_xferred_update (Type: Counter)
  total number of bytes updated/written
    Metric  Labels                                                                Value 
    ------  ------                                                                ----- 
    Counter (jobid=dfuse, pid=2688260, pool=fd69d9b8-c863-4bd8-8fc2-59db53ad8cdd) 0     

- Metric Set: client_started_at (Type: Gauge)
  Timestamp of client startup
    Metric Labels                     Value           
    ------ ------                     -----           
    Gauge  (jobid=dfuse, pid=2688260) 1.746038345e+09 

(Not shown: Go runtime metrics)
```