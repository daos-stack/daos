# DAOS Performance Tuning

This section documents how to validate the performance of the baseline
building blocks in a DAOS system (i.e. network and storage) and then the full
stack.

## Network Performance

The DAOS [CaRT][1] layer can validate and benchmark network communications in
the same context as an application and using the same networks/tuning options
as regular DAOS.

The CaRT `self_test` can run against the DAOS servers in a production environment
in a non-destructive manner. The only requirement is to have a formatted DAOS
system and the DAOS agent running on the client node where self\_test is run.

### Parameters

`self_test` supports different message sizes, bulk transfers, multiple targets,
and the following test scenarios:

-   Selftest client to servers - where `self_test` issues RPCs directly
    to a list of servers.

-   Cross-servers - where `self_test` sends instructions to the different
    servers that will issue cross-server RPCs. This model supports a
    many to many communication model.

The mode is selected via the `--master-endpoint` option. If this option is
notified on the command line, then we are in the first mode and the self_test
binary itself issues the RPCs. If one or several master endpoint are specified,
then we are in the cross-server mode.

An endpoint consists of pair of two values separated by a colon. The first value
is the rank that matches the engine rank displayed in `dmg system query`. The
second value is called tag and identified the service thread in the engine.
The DAOS engine uses the following mapping:

- tag 0 is used by the metadata service handling pool and container operations.
- tag 1 is used for cross-server monitoring (SWIM).
- tags 2 to [#targets + 1] is used by DAOS targets (one tag per target].
- tags [#targets + 2] to [#targets + #helpers + 1] is used by helper service threads.

As an example, an engine with `targets: 16` and `nr_xs_helpers: 4` would use the
following tag distributions:

- tag 0: metadata service
- tag 1: monitoring service
- tag 2-17: targets 0 to 15 (16 targets total)
- tag 18-21: helper service

For a total of 21 endpoints exported by this engine.

The RPC flow sent over the network can be configured via the `--message-sizes`
options that take a list of size tuples. Performance will be reported individually
for each tuple. Each size integer can be prepended with a single character to specify
the underlying transport mechanism. Available types are:

- 'e' - Empty (no payload)
- 'i' - I/O vector (IOV)
- 'b' - Bulk transfer

For example, (b1000) would transfer 1000 bytes via bulk in both directions. Similarly,
(i100 b1000) would use IOV to send and bulk to reply.

`--repetitions-per-size` can be used to define the Number of samples per message
size per endpoint with a default value of 10000. `--max-inflight-rpcs`
determines the number of concurrent RPCs issued simultaneously.

`self_test` has many more options. For a full description of `self_test` usage,
please run:

```bash
$ self_test --help
```

### Example: Client-to-Servers

To run self\_test in client-to-servers mode:

```bash
self_test -u --group-name daos_server --endpoint 0:2 --message-size '(0 b1048578)' --max-inflight-rpcs 16 --repetitions 100000
```

This will send 100k RPCs with a empty request, a bulk put of 1MB followed by an
empty reply from the node where the self_test application is running to the
first target of engine rank 0. This workload effectively simulate a 1MB
fetch/read RPC over DAOS.

A 1MB update/write RPC would be simulated with the following command:

```bash
self_test -u --group-name daos_server --endpoint 0:2 --message-size "(b1048578 0)" --max-inflight-rpcs 16 --repetitions 100000
```

The RPC rate with empty request and reply is also often useful to evaluate what
is the maximum capabilities of the network. This can be achieved as with the
following command line:

```bash
self_test -u --group-name daos_server --endpoint 0:2 --message-size "(0 0)" --max-inflight-rpcs 16 --repetitions 100000
```

0 could be replaced with i2048 for instance to send a payload of 2Kb.

All those 3 tests could be combined in a single and unique run:

```bash
self_test -u --group-name daos_server --endpoint 0:2 --message-size "(0 0) (b1048578 0) (0 b1048578)" --max-inflight-rpcs 16 --repetitions 100000
```

RPCs could also be send to a range of engine ranks and tags as follows:

```bash
self_test -u --group-name daos_server --endpoint 0-<MAX_RANK>:0-<MAX_TAG> --message-size "(0 0) (b1048578 0) (0 b1048578)" --max-inflight-rpcs 16 --repetitions 100000
```

!!! note
    By default, self\_test will use the network interface selected by the agent.
    This can be forced by setting the OFI\_INTERFACE and OFI\_DOMAIN environment
    variables manually. e.g. export OFI\_INTERFACE=eth0; export OFI\_DOMAIN=eth0
    or export OFI\_INTERFACE=ib0; export OFI\_DOMAIN=mlx5_0

!!! note
    Depending on the HW configuration, the agent might assign a different
    network interface to the self_test application depending on the NUMA node
    where the process is scheduled. It is thus recommended to use taskset to bind
    the self_test process to a specific core. e.g. taskset -c 1 self_test ...

### Example: Cross-Servers

To run self\_test in cross-servers mode:

```bash

$ self_test -u --group-name daos_server --endpoint 0-<MAX_SERVER-1>:0 \
  --master-endpoint 0-<MAX_RANK>:0-<MAX_TAG> \
  --message-sizes "b1048576,b1048576 0,0 b1048576,i2048,i2048 0,0 i2048" \
  --max-inflight-rpcs 16 --repetitions 100
```

The commands above will run `self_test` benchmark using the following message sizes:
```bash
b1048576     1Mb bulk transfer Get and Put
b1048576 0   1Mb bulk transfer Get only
0 b1048576   1Mb bulk transfer Put only
I2048        2Kb iovec Input and Output
i2048 0      2Kb iovec Input only
0 i2048      2Kb iovec Output only
```

!!! note
    Number of repetitions, max inflight rpcs, message sizes can be adjusted based
    on the particular test/experiment.

## Storage Performance

### SCM

DAOS provides a tool called `vos_perf` to benchmark the versioning object store
over storage-class memory. For a full description of `vos_perf` usage, please
run:

```bash
$ vos_perf --help
```

The `-R` option is used to define the operation to be performanced:

- `U` for `update` (i.e. write) operation
- `F` for `fetch` (i.e. read) operation
- `P` for `punch` (i.e. truncate) operation
- `p` to display the performance result for the previous operation.

For instance, -R "U;p F;p" means update the keys, print the update rate/bandwidth,
fetch the keys and then print the fetch rate/bandwidth. The number of
object/dkey/akey/value can be passed via respectively the -o, -d, -a and -n
options. The value size is specified via the -s parameter (e.g. -s 4K for 4K
value).

For instance, to measure rate for 10M update & fetch operation in VOS mode,
mount the pmem device and then run:

```bash
$ cd /mnt/daos0
$ df .
Filesystem      1K-blocks  Used  Available Use% Mounted on
/dev/pmem0     4185374720 49152 4118216704   1% /mnt/daos0
$ taskset -c 1 vos_perf -D . -P 100G -d 10000000 -a 1 -n 1 -s 4K -z -R "U;p F;p"
Test :
        VOS (storage only)
Pool :
        a3b7ff28-56ff-4974-9283-62990dd770ad
Parameters :
        pool size     : SCM: 102400 MB, NVMe: 0 MB
        credits       : -1 (sync I/O for -ve)
        obj_per_cont  : 1 x 1 (procs)
        dkey_per_obj  : 10000000 (buf)
        akey_per_dkey : 1
        recx_per_akey : 1
        value type    : single
        stride size   : 4096
        zero copy     : yes
        VOS file      : ./vos0
Running test=UPDATE
Running UPDATE test (iteration=1)
UPDATE successfully completed:
        duration  : 124.478568 sec
        bandwidth : 313.809    MB/sec
        rate      : 80335.11   IO/sec
        latency   : 12.448     us (nonsense if credits > 1)
Duration across processes:
        MAX duration : 124.478568 sec
        MIN duration : 124.478568 sec
        Average duration : 124.478568 sec
Completed test=UPDATE
Running test=FETCH
Running FETCH test (iteration=1)
FETCH successfully completed:
        duration  : 23.884087  sec
        bandwidth : 1635.503   MB/sec
        rate      : 418688.81  IO/sec
        latency   : 2.388      us (nonsense if credits > 1)
Duration across processes:
        MAX duration : 23.884087  sec
        MIN duration : 23.884087  sec
        Average duration : 23.884087  sec
```

Taskset is used to change the CPU affinity of the daos\_perf process.

!!! warning
    Performance of persistent memory may be impacted by NUMA affinity. It is
    thus recommended to set the affinity of daos\_perf to a CPU core locally
    attached to the persistent memory device.

The same test can be performed on the 2nd pmem device to compare the
performance.

```
$ cd /mnt/daos1/
$ df .
Filesystem      1K-blocks   Used  Available Use% Mounted on
/dev/pmem1     4185374720 262144 4118003712   1% /mnt/daos1
$ taskset -c 36 vos_perf -D . -P 100G -d 10000000 -a 1 -n 1 -s 4K -z -R "U;p F;p"
Test :
        VOS (storage only)
Pool :
        9d6c3fbd-a4f1-47d2-92a9-6112feb52e74
Parameters :
        pool size     : SCM: 102400 MB, NVMe: 0 MB
        credits       : -1 (sync I/O for -ve)
        obj_per_cont  : 1 x 1 (procs)
        dkey_per_obj  : 10000000 (buf)
        akey_per_dkey : 1
        recx_per_akey : 1
        value type    : single
        stride size   : 4096
        zero copy     : yes
        VOS file      : ./vos0
Running test=UPDATE
Running UPDATE test (iteration=1)
UPDATE successfully completed:
        duration  : 123.389467 sec
        bandwidth : 316.579    MB/sec
        rate      : 81044.19   IO/sec
        latency   : 12.339     us (nonsense if credits > 1)
Duration across processes:
        MAX duration : 123.389467 sec
        MIN duration : 123.389467 sec
        Average duration : 123.389467 sec
Completed test=UPDATE
Running test=FETCH
Running FETCH test (iteration=1)
FETCH successfully completed:
        duration  : 24.114830  sec
        bandwidth : 1619.854   MB/sec
        rate      : 414682.58  IO/sec
        latency   : 2.411      us (nonsense if credits > 1)
Duration across processes:
        MAX duration : 24.114830  sec
        MIN duration : 24.114830  sec
        Average duration : 24.114830  sec
Completed test=FETCH
```

Bandwidth can be tested by using a larger record size (i.e. -s option). For
instance:

```
$ taskset -c 36 vos_perf -D . -P 100G -d 40000 -a 1 -n 1 -s 1M -z -R "U;p F;p"
Test :
        VOS (storage only)
Pool :
        dc44f0dd-930e-43b1-b599-5cc141c868d9
Parameters :
        pool size     : SCM: 102400 MB, NVMe: 0 MB
        credits       : -1 (sync I/O for -ve)
        obj_per_cont  : 1 x 1 (procs)
        dkey_per_obj  : 40000 (buf)
        akey_per_dkey : 1
        recx_per_akey : 1
        value type    : single
        stride size   : 1048576
        zero copy     : yes
        VOS file      : ./vos0
Running test=UPDATE
Running UPDATE test (iteration=1)
UPDATE successfully completed:
        duration  : 21.247287  sec
        bandwidth : 1882.593   MB/sec
        rate      : 1882.59    IO/sec
        latency   : 531.182    us (nonsense if credits > 1)
Duration across processes:
        MAX duration : 21.247287  sec
        MIN duration : 21.247287  sec
        Average duration : 21.247287  sec
Completed test=UPDATE
Running test=FETCH
Running FETCH test (iteration=1)
FETCH successfully completed:
        duration  : 10.133850  sec
        bandwidth : 3947.167   MB/sec
        rate      : 3947.17    IO/sec
        latency   : 253.346    us (nonsense if credits > 1)
Duration across processes:
        MAX duration : 10.133850  sec
        MIN duration : 10.133850  sec
        Average duration : 10.133850  sec
Completed test=FETCH
```

!!! note
    With 3rd Gen Intel® Xeon® Scalable processors (ICX), the PMEM_NO_FLUSH
    environment variable can be set to 1 to take advantage of the extended
    asynchronous DRAM refresh (eADR) feature

A tool called daos\_perf with the same syntax as vos\_perf is also available
to run tests from a compute node with the full DAOS stack. Please refer
to the next section for more information.

### SSDs

Performance of SSDs can be measured directly with SPDK via the spdk_nvme_perf
tool. It can be run to test bandwidth in a non-destructive way as follows:

```bash
spdk_nvme_perf -q 16 -o 1048576 -w read -c 0xff -t 60
```

IOPS can be measured with the following command:
```bash
spdk_nvme_perf -q 16 -o 4096 -w read -c 0xff -t 60
```

`-q` is used to control the queue depth, `-o` for the I/O size, `-w` is the
operation and can be either (rand)read, (rand)write or (rand)rw. The test
duration (in minutes) is defined by the `-t` parameter.

!!! warning
    *write and *rw options are destructive.

This command uses all the available SSDs. Specific SSDs can be specified via the
`--allowed-pci-addr` options followed by the PCIe addresses of the SSDs of
interest.

The `-c` option is used to specify the CPU cores used to submit I/O under the
form of a core mash. `-c 0xff` uses the first 8 cores.

!!! note
    On storage node using Intel VMD, the `--enable-vmd` option must be specified.

Many more options are available. Please run `spdk_nvme_perf` to see the list of
parameters that can be tweaked.

## End-to-end Performance

DAOS can be benchmarked using several widely used IO benchmarks like IOR,
mdtest, and FIO. There are several backends that can be used with those
benchmarks.

### ior

IOR (<https://github.com/hpc/ior>) with the following backends:

-   The IOR APIs POSIX, MPIIO and HDF5 can be used with DAOS POSIX containers
    that are accessed over dfuse. This works without or with the I/O
    interception library (`libioil`). Performance is significantly better when
    using `libioil`. For detailed information on dfuse usage with the IO
    interception library, please refer to the [POSIX DFUSE section][7].

-   A custom DFS (DAOS File System) plugin for DAOS can be used by building IOR
    with DAOS support, and selecting API=DFS. This integrates IOR directly with the
    DAOS File System (`libdfs`), without requiring FUSE or an interception library.
    Please refer to the [DAOS README][10] in the hpc/ior repository for some basic
    instructions on how to use the DFS driver.

-   When using the IOR API=MPIIO, the ROMIO ADIO driver for DAOS can be used by
    providing the `daos://` prefix to the filename. This ADIO driver bypasses `dfuse`
    and directly invkes the `libdfs` calls to perform I/O to a DAOS POSIX container.
    The DAOS-enabled MPIIO driver is available in the upstream MPICH repository and
    included with Intel MPI. Please refer to the [MPI-IO documentation][8].

-   An HDF5 VOL connector for DAOS is under development. This maps the HDF5 data model
    directly to the DAOS data model, and works in conjunction with DAOS containers of
    `--type=HDF5` (in contrast to DAOS container of `--type=POSIX` that are used for
    the other IOR APIs). Please refer the the [HDF5 with DAOS documentation][9].

IOR has several parameters to characterize performance. The main parameters to
work with include:

- transfer size (-t)
- block size (-b)
- segment size (-s)

For more use cases, the IO-500 workloads are a good starting point to measure
performance on a system: https://github.com/IO500/io500

### mdtest

mdtest is released in the same repository as IOR. The corresponding backends
that are listed above support mdtest, except for the MPI-IO and HDF5 backends
that were only designed to support IOR. The [DAOS README][10] in the hpc/ior
repository includes some examples to run mdtest with DAOS.

The IO-500 workloads for mdtest provide some good criteria for performance
measurements.

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

### daos\_perf

Finally, DAOS provides a tool called `daos_perf` which allows benchmarking to the
DAOS object API directly. It has a similar syntax as `vos_perf` and, like IOR,
can be run as an MPI application. For a full description of `daos_perf` usage,
please run:

```bash
$ daos_perf --help
```

Like `vos_perf`, the `-R` option is used to define the operation to be performanced:

- `U` for `update` (i.e. write) operation
- `F` for `fetch` (i.e. read) operation
- `P` for `punch` (i.e. truncate) operation
- `p` to display the performance result for the previous operation.

For instance, -R "U;p F;p" means update the keys, print the update rate/bandwidth,
fetch the keys and then print the fetch rate/bandwidth. The number of
object/dkey/akey/value can be passed via respectively the -o, -d, -a and -n
options. The value size is specified via the -s parameter (e.g. -s 4K for 4K
value).

## Client Tuning

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
Agent's environment variable `DAOS_AGENT_DISABLE_CACHE=true` or updating the
Agent configuration file with `disable_caching: true`.

If the network configuration changes while the Agent is running, and the cache
is enabled, the Agent must be restarted to gain visibility to these changes.
For additional information, please refer to the
[System Deployment: Agent Startup][6] documentation section.

[1]: <https://github.com/daos-stack/daos/blob/master/src/cart#readme> (Collective and RPC Transport)
[2]: <installation.md#distribution-packages> (DAOS distribution packages)
[3]: <installation.md#building-daos--dependencies> (DAOS build documentation)
[4]: <deployment.md#server-startup> (DAOS server startup documentation)
[5]: <https://www.open-mpi.org/faq/?category=running#mpirun-hostfile> (mpirun hostfile)
[6]: <deployment.md#disable-agent-cache-optional> (System Deployment Agent Startup)
[7]: <../user/posix/#dfuse>
[8]: <../user/mpi-io.md>
[9]: <../user/hdf5.md>
[10]: <https://github.com/hpc/ior/blob/main/README_DAOS>
