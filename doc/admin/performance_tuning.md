# DAOS Performance Tuning

This section will be expanded in a future revision.

## Network Performance

The DAOS [CaRT][1] layer can validate and benchmark network communications in
the same context as an application and using the same networks/tuning options
as regular DAOS.

The CaRT `self_test` can run against the DAOS servers in a production environment
in a non-destructive manner. CaRT `self_test` supports different message sizes,
bulk transfers, multiple targets, and the following test scenarios:

-   **Selftest client to servers** - where `self_test` issues RPCs directly
    to a list of servers.

-   **Cross-servers** - where `self_test` sends instructions to the different
    servers that will issue cross-server RPCs. This model supports a
    many to many communication model.

### Getting DAOS CaRT self_test

The CaRT `self_test` and its tests are delivered as part of the daos_client
and daos_tests [distribution packages][2]. It can also be built from scratch.

```bash
$ git clone --recurse-submodules https://github.com/daos-stack/daos.git
$ cd daos
$ scons --build-deps=yes install
$ cd install
```

For detailed information, please refer to the [DAOS build documentation][3]
section.

### Running CaRT self_test

Instructions to run CaRT `self_test` with test_group as the target server are
as follows.

**Prepare server_hostfile and client_hostfile**

-   **server_hostfile** - contains a list of nodes from which servers will
    launch.

-   **client_hostfile** - contains a list of nodes from which `self_test` will
    launch.

The example below uses an Ethernet interface and Sockets provider.
In the `self_test` commands:

-   **Selftest client to servers** - Replace the argument for `--endpoint`
    accordingly.

-   **Cross-servers** - Replace the argument for `--endpoint` and
    `--master-endpoint` accordingly.

For example, if you have 8 servers, you would specify `--endpoint 0-7:0` and
`--master-endpoint 0-7:0`

The commands below will run `self_test` benchmark using the following message sizes:
```bash
b1048576     1Mb bulk transfer Get and Put
b1048576 0   1Mb bulk transfer Get only
0 b1048576   1Mb bulk transfer Put only
I2048        2Kb iovec Input and Output
i2048 0      2Kb iovec Input only
0 i2048      2Kb iovec Output only
```

For a full description of `self_test` usage, run:

```bash
$ ./bin/self_test --help
```

**To start test_group server:**
```bash
$ /usr/lib64/openmpi3/bin/orterun --mca btl self,tcp -N 1 \
  --hostfile server_hostfile --output-filename testLogs/ \
  -x D_LOG_FILE=testLogs/test_group_srv.log -x D_LOG_FILE_APPEND_PID=1 \
  -x D_LOG_MASK=WARN -x CRT_PHY_ADDR_STR=ofi+sockets -x OFI_INTERFACE=eth0 \
  -x CRT_CTX_SHARE_ADDR=0 -x CRT_CTX_NUM=16 -x CRT_ATTACH_INFO_PATH=. \
  ./bin/crt_launch -e lib/daos/TESTING/tests/test_group_np_srv \
  --name self_test_srv_grp --cfg_path=. &
```

**To run self_test in client-to-servers mode:**
```bash
$ /usr/lib64/openmpi3/bin/orterun --mca btl self,tcp -N 1 \
  --hostfile client_hostfile --output-filename testLogs/ \
  -x D_LOG_FILE=testLogs/self_test.log -x D_LOG_FILE_APPEND_PID=1 \
  -x D_LOG_MASK=WARN -x CRT_PHY_ADDR_STR=ofi+sockets -x OFI_INTERFACE=eth0 \
  -x CRT_CTX_SHARE_ADDR=0 -x CRT_CTX_NUM=16 \
  ./bin/self_test --group-name self_test_srv_grp --endpoint 0-<MAX_SERVER-1>:0 \
  --message-sizes "b1048576,b1048576 0,0 b1048576,i2048,i2048 0,0 i2048" \
  --max-inflight-rpcs 16 --repetitions 100 -t -n -p .
```

**To run self_test in cross-servers mode:**
```bash
$ /usr/lib64/openmpi3/bin/orterun --mca btl self,tcp -N 1 \
  --hostfile client_hostfile --output-filename testLogs/ \
  -x D_LOG_FILE=testLogs/self_test.log -x D_LOG_FILE_APPEND_PID=1 \
  -x D_LOG_MASK=WARN -x CRT_PHY_ADDR_STR=ofi+sockets -x OFI_INTERFACE=eth0 \
  -x CRT_CTX_SHARE_ADDR=0 -x CRT_CTX_NUM=16  \
  ./bin/self_test --group-name self_test_srv_grp --endpoint 0-<MAX_SERVER-1>:0 \
  --master-endpoint 0-<MAX_SERVER-1>:0 \
  --message-sizes "b1048576,b1048576 0,0 b1048576,i2048,i2048 0,0 i2048" \
  --max-inflight-rpcs 16 --repetitions 100 -t -n -p .
```

**To shutdown test_group server:**
```bash
$ /usr/lib64/openmpi3/bin/orterun --mca btl self,tcp  -N 1 \
  --hostfile client_hostfile --output-filename testLogs/ \
  -x D_LOG_FILE=testLogs/test_group_cli.log \
  -x D_LOG_FILE_APPEND_PID=1 -x D_LOG_MASK=WARN \
  -x CRT_PHY_ADDR_STR=ofi+sockets -x OFI_INTERFACE=eth0 \
  -x CRT_CTX_SHARE_ADDR=0 \
  lib/daos/TESTING/tests/test_group_np_cli --name client-group \
  --attach_to self_test_srv_grp --shut_only --cfg_path=.
```

## Benchmarking DAOS

DAOS can be benchmarked using several widely used IO benchmarks like IOR,
mdtest, and FIO. There are several backends that can be used with those
benchmarks.

### ior

IOR (<https://github.com/hpc/ior>) with the following backends:

-   POSIX, MPIIO & HDF5 drivers over dfuse and the interception library.

-   MPI-IO plugin with the ROMIO DAOS ADIO driver to bypass POSIX and dfuse. The
    MPIIO driver is available in the upstream MPICH repository.

-   HDF5 plugin with the HDF5 DAOS connector (under development). This maps the
    HDF5 data model directly to the DAOS model bypassing POSIX.

-   A custom DFS (DAOS File System) plugin, integrating IOR directly with libfs
    without requiring FUSE or an interception library

-   A custom DAOS plugin, integrating IOR directly with the native DAOS
    array API.

### mdtest

mdtest is released in the same repository as IOR. The corresponding backends that are
listed above support mdtest, except for the MPI-IO and HDF5 backends that were
only designed to support IOR.

### FIO

FIO can also be used to benchmark DAOS performance using dfuse and the
interception library with all the POSIX based engines like sync and libaio. We
do, however, provide a native DFS engine for FIO similar to what we do for
IOR. That engine is available on GitHub: <https://github.com/daos-stack/dfio>

### daos_perf

Finally, DAOS provides a tool called `daos_perf` which allows benchmarking to the
DAOS object API directly or to the internal VOS API, which bypasses the client
and network stack and reports performance accessing the storage directly using
VOS. For a full description of `daos_perf` usage, run:

```bash
$ daos_perf --help
```

## Client Performance Tuning

For best performance, a DAOS client should specifically bind itself to a NUMA
node instead of leaving core allocation and memory binding to chance.  This
allows the DAOS Agent to detect the client's NUMA affinity from its PID and
automatically assign a network interface with a matching NUMA node.  The network
interface provided in the GetAttachInfo response is used to initialize CaRT.

To override the automatically assigned interface, the client should set the
environment variable `OFI_INTERFACE` to match the desired network
interface.

The DAOS Agent scans the client machine on the first GetAttachInfo request to
determine the set of network interfaces available that support the DAOS Server's
OFI provider.  This request occurs as part of the initialization sequence in the
`libdaos daos_init()` performed by each client.

Upon receipt, the Agent populates a cache of responses indexed by NUMA affinity.
Provided a client application has bound itself to a specific NUMA node and that
NUMA node has a network device associated with it, the DAOS Agent will provide a
GetAttachInfo response with a network interface corresponding to the client's
NUMA node.

When more than one appropriate network interface exists per NUMA node, the agent
uses a round-robin resource allocation scheme to load balance the responses for
that NUMA node.

If a client is bound to a NUMA node that has no matching network interface, then
a default NUMA node is used for the purpose of selecting a response.  Provided
that the DAOS Agent can detect any valid network device on any NUMA node, the
default response will contain a valid network interface for the client.  When a
default response is provided, a message in the Agent's log is emitted:

```
No network devices bound to client NUMA node X.  Using response from NUMA Y
```

To improve performance, it is worth figuring out if the client bound itself to
the wrong NUMA node, or if expected network devices for that NUMA node are
missing from the Agent's fabric scan.

In some situations, the Agent may detect no network devices and the response
cache will be empty.  In such a situation, the GetAttachInfo response will
contain no interface assignment and the following info message will be found in
the Agent's log:

```
No network devices detected in fabric scan; default AttachInfo response may be incorrect
```

In either situation, the admin may execute the command
`daos_agent net-scan` with appropriate debug flags to gain more insight
into the configuration problem.

**Disabling the GetAttachInfo cache:**

The default configuration enables the Agent GetAttachInfo cache.  If it is
desired, the cache may be disabled prior to DAOS Agent startup by setting the
Agent's environment variable `DAOS_AGENT_DISABLE_CACHE=true`.  The cache is
loaded only at Agent startup. The following debug message will be found in the
Agent's log:
```
GetAttachInfo agent caching has been disabled
```

If the network configuration changes while the Agent is running, it must be
restarted to gain visibility to these changes. For additional information,
please refer to the [System Deployment: Agent Startup][4] documentation
section.

[1]: <https://github.com/daos-stack/daos/tree/master/src/cart> (Collective and RPC Transport)
[2]: <https://github.com/daos-stack/daos/blob/master/doc/admin/installation.md#distribution-packages> (DAOS distribution packages)
[3]: <https://github.com/daos-stack/daos/blob/master/doc/admin/installation.md#building-daos--dependencies> (DAOS build documentation)
[4]: <https://github.com/daos-stack/daos/blob/master/doc/admin/deployment.md#disable-agent-cache-optional> (System Deployment Agent Startup)
