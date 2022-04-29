# DAOS Performance Tuning

## Network Performance

The DAOS [CaRT][1] layer can validate and benchmark network communications in
the same context as an application and using the same networks/tuning options
as regular DAOS.

The CaRT `self_test` can run against the DAOS servers in a production environment
in a non-destructive manner. CaRT `self_test` supports different message sizes,
bulk transfers, multiple targets, and the following test scenarios:

- Self-test client to servers: where `self_test` issues RPCs directly to a list of servers.

- Cross-servers: where `self_test` sends instructions to the different servers that will issue cross-server RPCs. This model supports a many-to-many communication model.

### Building CaRT self_test

The CaRT `self_test` and its tests are delivered as part of the daos_client
and daos_tests [distribution packages][2]. It can also be built from scratch.

```bash
git clone --recurse-submodules https://github.com/daos-stack/daos.git
cd daos
scons --build-deps=yes install
cd install
```

For detailed information, please refer to the [DAOS build documentation][3]
section.

### Running CaRT self_test

Instructions to run CaRT `self_test` are as follows.

#### Start DAOS server

`self_test` requires the DAOS server to be running before attempt running
`self_test`. For detailed instructions on starting the DAOS server, please refer
to the [server startup][4] documentation.

#### Dump system attachinfo

`self_test` will use the address information in `daos_server.attach_info_tmp`
file. To create such file, run the following command:

```bash
./bin/daos_agent dump-attachinfo -o ./daos_server.attach_info_tmp
```

#### Prepare hostfile

The list of nodes from which `self_test` will run can be specified in a
hostfile (referred to as ${hostfile}). Hostfile used here is the same as the
ones used by OpenMPI. Please refer too the
[mpirun documentation][5] for additional details

#### Run CaRT self_test

The example below uses an Ethernet interface and TCP provider.
In the `self_test` commands:

- Selftest client to servers: Replace the argument for `--endpoint`
    accordingly.

- Cross-servers: Replace the argument for `--endpoint` and `--master-endpoint` accordingly.

For example, if you have eight servers, you would specify `--endpoint 0-7:0` and
`--master-endpoint 0-7:0`

The commands below will run the `self_test` benchmark using the following message sizes:

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
./bin/self_test --help
```

#### To run self_test in client-to-servers mode

(Assuming sockets provider over eth0)

```bash

# Specify provider
export CRT_PHY_ADDR_STR='ofi+tcp'

# Specify interface
export OFI_INTERFACE=eth0

# Specify domain; usually only required when running over ofi+verbs;ofi_rxm
# For example, in such configuration, OFI_DOMAIN might be set to mlx5_0
# run fi_info --provider='verbs;ofi_rxm' to find an appropriate domain
# If only specifying OFI_INTERFACE without OFI_DOMAIN, we assume we do not need one
export OFI_DOMAIN=eth0

# Export additional CART-level environment variables as described in README.env
# If needed. For example, export D_LOG_FILE=/path/to/log will allow the dumping of the
# Log into the file instead of stdout/stderr

$ ./bin/self_test --group-name daos_server --endpoint 0-<MAX_SERVER-1>:0 \
  --message-sizes "b1048576,b1048576 0,0 b1048576,i2048,i2048 0,0 i2048" \
  --max-inflight-rpcs 16 --repetitions 100 -p /path/to/attach_info
```

#### To run self_test in cross-servers mode

```bash

$ ./bin/self_test --group-name daos_server --endpoint 0-<MAX_SERVER-1>:0 \
  --master-endpoint 0-<MAX_SERVER-1>:0 \
  --message-sizes "b1048576,b1048576 0,0 b1048576,i2048,i2048 0,0 i2048" \
  --max-inflight-rpcs 16 --repetitions 100 -p /path/to/attach_info
```

Note:
The number of repetitions, max inflight rpcs, message sizes can be adjusted based on the
particular test/experiment.

## Benchmarking DAOS

DAOS can be benchmarked using several widely used IO benchmarks like IOR,
mdtest, and FIO. Several backends can be used with those
benchmarks.

### IOR

IOR (<https://github.com/hpc/ior>) with the following backends:

- The IOR APIs POSIX, MPIIO and HDF5 can be used with DAOS POSIX containers
    accessed over dfuse. This works without or with the I/O
    interception library (`libioil`). Performance is significantly better when
    using `libioil`. For detailed information on dfuse usage with the IO
    interception library, please refer to the [POSIX DFUSE section][7].

- A custom DFS (DAOS File System) plugin for DAOS can be used by building IOR
    with DAOS support and selecting API=DFS. This integrates IOR directly with the
    DAOS File System (`libdfs`), without requiring FUSE or an interception library.
    Please refer to the [DAOS README][10] in the hpc/ior repository for some basic
    instructions on how to use the DFS driver.

- When using the IOR API=MPIIO, the ROMIO ADIO driver for DAOS can be used by
    providing the `daos://` prefix to the filename. This ADIO driver bypasses `dfuse`
    and directly invkes the `libdfs` calls to perform I/O to a DAOS POSIX container.
    The DAOS-enabled MPIIO driver is available in the upstream MPICH repository and
    included with Intel MPI. Please refer to the [MPI-IO documentation][8].

- An HDF5 VOL connector for DAOS is under development. This maps the HDF5 data model
    directly to the DAOS data model and works in conjunction with DAOS containers of
    `--type=HDF5` (in contrast to the DAOS container of `--type=POSIX` used for
    the other IOR APIs). Please refer the [HDF5 with DAOS documentation][9].

IOR has several parameters to characterize the performance. The main parameters to
work with include:

- transfer size (-t)
- block size (-b)
- segment size (-s)

For more use cases, the [IO-500 workloads](https://github.com/IO500/io500) are a good starting point to measure
performance on a system

### mdtest

mdtest is released in the same repository as IOR. The corresponding backends
listed above support mdtest, except for the MPI-IO and HDF5 backends
that were only designed to support IOR. The [DAOS README][10] in the hpc/ior
repository includes some examples to run mdtest with DAOS.

The IO-500 workloads for mdtest provide some excellent criteria for performance
measurements.

### FIO

A DAOS engine is integrated into FIO and available upstream.
To build it, just run:

```bash
git clone http://git.kernel.dk/fio.git
cd fio
./configure
make install
```

If DAOS is installed via packages, it should be automatically detected.
If not, please specify the path to the DAOS library and headers to configure
as follows:

```bash
CFLAGS="-I/path/to/daos/install/include" LDFLAGS="-L/path/to/daos/install/lib64" ./configure
```

Once successfully built, one can run the default example:

```bash
export POOL= # your pool UUID
export CONT= # your container UUID
fio ./examples/dfs.fio
```

Please note that DAOS does not transfer data (i.e., zeros) over the network
when reading a hole in a sparse POSIX file. Very high read bandwidth can
thus be reported if fio reads unallocated extents in a file. It is thus
an excellent practice to start fio with a first write phase.

FIO can also be used to benchmark DAOS performance using dfuse and the
interception library with all the POSIX-based engines like sync and libaio.

### daos_perf & vos_perf

Finally, DAOS provides a tool called `daos_perf`, which allows benchmarking to the
DAOS object API directly and a tool called 'vos_perf' to benchmark the internal
VOS API, which bypasses the client and network stack and reports performance
accessing the storage directly using VOS. For a complete description of `daos_perf` or
`vos_perf` usage, run:

```bash
daos_perf --help
```

```bash
vos_perf --help
```

The `-R` option is used to define the operation to be performed:

- `U` for `update` (i.e., write) operation
- `F` for `fetch` (i.e., read) operation
- `P` for `punch` (i.e., truncate) operation
- `p` to display the performance result for the previous operation.

For instance, -R "U;p F;p" means update the keys, print the update rate/bandwidth,
fetch the, keys and then print the fetch rate/bandwidth. The number of
object/dkey/akey/value can be passed via respectively the -o, -d, -a and -n
options. The value size is specified via the -s parameter (e.g., -s 4K for 4K
value).

For instance, to measure the rate for 10M update & fetch operation in VOS mode,
mount the PMem device and then run:

```bash
cd /mnt/daos0
df .
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

```bash
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
        MAX duration: 123.389467 sec
        MIN duration: 123.389467 sec
        Average duration: 123.389467 sec
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

```bash
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
    An environment variable can be set to 1 to take advantage of the extended
    asynchronous DRAM refresh (eADR) feature

In DAOS mode, daos\_perf can be used as an MPI application like IOR.
Parameters are the same, except that `-T daos` can be used to select the daos
mode. This option can be omitted since this is the default.

## Client Performance Tuning

A DAOS client should specifically bind itself to an NUMA
node for best performance instead of leaving core allocation and memory binding to chance.  This
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
NUMA node has a network device associated with it; the DAOS Agent will provide a
GetAttachInfo response with a network interface corresponding to the client's
NUMA node.

When more than one appropriate network interface exists per NUMA node, the agent
uses a round-robin resource allocation scheme to load balance the responses for
that NUMA node.

If a client is bound to a NUMA node with no matching network interface, then
a default NUMA node is used to select a response.  Provided
that the DAOS Agent can detect any valid network device on any NUMA node, the
default response will contain a valid network interface for the client.  When a
default response is provided, a message in the Agent's log is emitted:

```bash
No network devices bound to client NUMA node X.  Using response from NUMA Y
```

To improve performance, it is worth figuring out if the client bound itself to
the wrong NUMA node, or if expected network devices for that NUMA node are
missing from the Agent's fabric scan.

In some situations, the Agent may detect no network devices and the response
The cache will be empty.  In such a situation, the GetAttachInfo response will
contain no interface assignment, and the following info message will be found in
the Agent's log:

```bash
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

If the network configuration changes while the Agent is running and the cache
is enabled, the Agent must be restarted to gain visibility of these changes.
For additional information, please refer to the
[System Deployment: Agent Startup][6] documentation section.

[^1]: <https://github.com/daos-stack/daos/blob/release/2.2/src/cart#readme> (Collective and RPC Transport)
[^2]: <installation.md#distribution-packages> (DAOS distribution packages)
[^3]: <installation.md#building-daos--dependencies> (DAOS build documentation)
[^4]: <deployment.md#server-startup> (DAOS server startup documentation)
[^5]: <https://www.open-mpi.org/faq/?category=running#mpirun-hostfile> (mpirun hostfile)
[^6]: <deployment.md#disable-agent-cache-optional> (System Deployment Agent Startup)
[^7]: <../user/posix/#dfuse>
[^8]: <../user/mpi-io.md>
[^9]: <../user/hdf5.md>
[^10]: <https://github.com/hpc/ior/blob/main/README_DAOS>
