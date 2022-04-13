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

### Building CaRT self_test

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

Instructions to run CaRT `self_test` are as follows.

**Start DAOS server**

`self_test` requires DAOS server to be running before attempt running
`self_test`. For detailed instruction on how to start DAOS server, please refer
to the [server startup][4] documentation.

**Dump system attachinfo**

`self_test` will use the address information in `daos_server.attach_info_tmp`
file. To create such file, run the following command:

```bash
./bin/daos_agent dump-attachinfo -o ./daos_server.attach_info_tmp
```

**Prepare hostfile**

The list of nodes from which `self_test` will run can be specified in a
hostfile (referred to as ${hostfile}). Hostfile used here is the same as the
ones used by OpenMPI. For additional details, please refer to the
[mpirun documentation][5].

**Run CaRT self_test**

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

**To run self_test in client-to-servers mode:**
```bash
$ /usr/lib64/openmpi3/bin/orterun --mca btl self,tcp -N 1 \
  --hostfile ${hostfile} --output-filename testLogs/ \
  -x D_LOG_FILE=testLogs/self_test.log -x D_LOG_FILE_APPEND_PID=1 \
  -x D_LOG_MASK=WARN -x CRT_PHY_ADDR_STR=ofi+sockets -x OFI_INTERFACE=eth0 \
  -x CRT_CTX_SHARE_ADDR=0 -x CRT_CTX_NUM=16 \
  ./bin/self_test --group-name daos_server --endpoint 0-<MAX_SERVER-1>:0 \
  --message-sizes "b1048576,b1048576 0,0 b1048576,i2048,i2048 0,0 i2048" \
  --max-inflight-rpcs 16 --repetitions 100 -t -n -p .
```

**To run self_test in cross-servers mode:**
```bash
$ /usr/lib64/openmpi3/bin/orterun --mca btl self,tcp -N 1 \
  --hostfile ${hostfile} --output-filename testLogs/ \
  -x D_LOG_FILE=testLogs/self_test.log -x D_LOG_FILE_APPEND_PID=1 \
  -x D_LOG_MASK=WARN -x CRT_PHY_ADDR_STR=ofi+sockets -x OFI_INTERFACE=eth0 \
  -x CRT_CTX_SHARE_ADDR=0 -x CRT_CTX_NUM=16  \
  ./bin/self_test --group-name daos_server --endpoint 0-<MAX_SERVER-1>:0 \
  --master-endpoint 0-<MAX_SERVER-1>:0 \
  --message-sizes "b1048576,b1048576 0,0 b1048576,i2048,i2048 0,0 i2048" \
  --max-inflight-rpcs 16 --repetitions 100 -t -n -p .
```

## Benchmarking DAOS

DAOS can be benchmarked using several widely used IO benchmarks like IOR,
mdtest, and FIO. There are several backends that can be used with those
benchmarks.

### ior

IOR (<https://github.com/hpc/ior>) with the following backends:

-   The IOR APIs POSIX, MPIIO and HDF5 can be used with DAOS POSIX containers that
    are accessed over dfuse. This works without or with the I/O interception library
    (`libioil`). Performance is significantly better when using `libioil`.

-   A custom DFS (DAOS File System) plugin for DAOS can be used by building IOR
    with DAOS support, and selecting API=DFS. This integrates IOR directly with the
    DAOS File System (`libdfs`), without requiring FUSE or an interception library.

-   When using the IOR API=MPIIO, the ROMIO ADIO driver for DAOS can be used by
    providing the `daos://` prefix to the filename. This ADIO driver bypasses `dfuse`
    and directly invkes the `libdfs` calls to perform I/O to a DAOS POSIX container.
    The DAOS-enabled MPIIO driver is available in the upstream MPICH repository
    (MPICH 3.4.1 or higher).

-   An HDF5 VOL connector for DAOS is under development. This maps the HDF5 data model
    directly to the DAOS data model, and works in conjunction with DAOS containers of
    `--type=HDF5` (in contrast to DAOS container of `--type=POSIX` that are used for
    the other IOR APIs).

### mdtest

mdtest is released in the same repository as IOR. The corresponding backends that are
listed above support mdtest, except for the MPI-IO and HDF5 backends that were
only designed to support IOR.

### FIO

A DAOS engine is integrated into FIO and available upstream.
To build it, just run:

```bash
$ git clone http://git.kernel.dk/fio.git
$ cd fio
$ ./configure
$ make install
```

If DAOS is installed via packages, it should be automatically detected.
If not, please specific the path to the DAOS library and headers to configure
as follows:
```
$ CFLAGS="-I/path/to/daos/install/include" LDFLAGS="-L/path/to/daos/install/lib64" ./configure
```

Once successfully build, once can run the default example:
```bash
$ export POOL= # your pool UUID
$ export CONT= # your container UUID
$ fio ./examples/dfs.fio
```

Please note that DAOS does not transfer data (i.e. zeros) over the network
when reading a hole in a sparse POSIX file. Very high read bandwidth can
thus be reported if fio reads unallocated extents in a file. It is thus
a good practice to start fio with a first write phase.

FIO can also be used to benchmark DAOS performance using dfuse and the
interception library with all the POSIX based engines like sync and libaio.

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
please refer to the [System Deployment: Agent Startup][6] documentation
section.

[1]: <https://github.com/daos-stack/daos/blob/release/1.2/src/cart> (Collective and RPC Transport)
[2]: <https://github.com/daos-stack/daos/blob/release/1.2/docs/admin/installation.md#distribution-packages> (DAOS distribution packages)
[3]: <https://github.com/daos-stack/daos/blob/release/1.2/docs/admin/installation.md#building-daos-and-dependencies> (DAOS build documentation)
[4]: <https://github.com/daos-stack/daos/blob/release/1.2/docs/admin/deployment.md#server-startup> (DAOS server startup documentation)
[5]: <https://www.open-mpi.org/faq/?category=running#mpirun-hostfile> (mpirun hostfile)
[6]: <https://github.com/daos-stack/daos/blob/release/1.2/docs/admin/deployment.md#disable-agent-cache-optional> (System Deployment Agent Startup)
