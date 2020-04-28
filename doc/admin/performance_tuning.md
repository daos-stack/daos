# DAOS Performance Tuning

This section will be expanded in a future revision.

## Network Performance

The DAOS CART layer can validate and benchmark network communications in the
same context as an application and using the same networks/tuning options as
regular DAOS.

The CART self_test can run against the DAOS servers in a production environment
in a non-destructive manner. CART self_test supports different message sizes,
bulk transfers, multiple targets, and the following test scenarios:

-   **Selftest client to servers** where self_test issues RPCs directly
    to a list of servers

-   **Cross-servers** where self_test sends instructions to the different
    servers that will issue cross-server RPCs. This model supports a
    many to many communication model.

Instructions to run CaRT self_test with test_group as the target server are as follows.

```bash
$ git clone https://github.com/daos-stack/daos.git
$ cd daos
$ git submodule init
$ git submodule update
$ scons --build-deps=yes install
$ cd install/TESTING
```

**Prepare srvhostfile and clihostfile**

-   srvhostfile contains list of nodes from which servers will launch

-   clihostfile contains node from which self_test will launch

The example below uses Ethernet interface and Sockets provider.
In the self_test commands:

-   (client-to-servers) Replace the argument for "--endpoint" accordingly.

-   (cross-servers)     Replace the argument for "--endpoint" and "--master-endpoint" accordingly.

-   For example, if you have 8 servers, you would specify "--endpoint 0-7:0" (and --master-endpoint 0-7:0)

The commands below will run self_test benchmark using the following message sizes:
```bash
b1048576     1Mb bulk transfer Get and Put
b1048576 0   1Mb bulk transfer Get only
0 b1048576   1Mb bulk transfer Put only
I2048        2Kb iovec Input and Output
i2048 0      2Kb iovec Input only
0 i2048      2Kb iovec Output only
```

For full description of self_test usage, run:
```bash
$ ../bin/self_test --help
```

**To start test_group server:**
```bash
$ /usr/lib64/openmpi3/bin/orterun --mca btl self,tcp  -N 1 --hostfile srvhostfile --output-filename testLogs/ -x D_LOG_FILE=testLogs/test_group_srv.log -x D_LOG_FILE_APPEND_PID=1 -x D_LOG_MASK=WARN -x CRT_PHY_ADDR_STR=ofi+sockets -x OFI_INTERFACE=eth0 -x CRT_CTX_SHARE_ADDR=0 -x CRT_CTX_NUM=16  ../bin/crt_launch -e tests/test_group_np_srv --name self_test_srv_grp --cfg_path=. &
```

**To run self_test in client-to-servers mode:**
```bash
$ /usr/lib64/openmpi3/bin/orterun --mca btl self,tcp  -N 1 --hostfile clihostfile --output-filename testLogs/ -x D_LOG_FILE=testLogs/self_test.log -x D_LOG_FILE_APPEND_PID=1 -x D_LOG_MASK=WARN -x CRT_PHY_ADDR_STR=ofi+sockets -x OFI_INTERFACE=eth0 -x CRT_CTX_SHARE_ADDR=0 -x CRT_CTX_NUM=16  ../bin/self_test --group-name self_test_srv_grp --endpoint 0-<MAX_SERVER-1>:0 --message-sizes "b1048576,b1048576 0,0 b1048576,i2048,i2048 0,0 i2048" --max-inflight-rpcs 16 --repetitions 100 -t -n -p .
```

**To run self_test in cross-servers mode:**
```bash
$ /usr/lib64/openmpi3/bin/orterun --mca btl self,tcp  -N 1 --hostfile clihostfile --output-filename testLogs/ -x D_LOG_FILE=testLogs/self_test.log -x D_LOG_FILE_APPEND_PID=1 -x D_LOG_MASK=WARN -x CRT_PHY_ADDR_STR=ofi+sockets -x OFI_INTERFACE=eth0 -x CRT_CTX_SHARE_ADDR=0 -x CRT_CTX_NUM=16  ../bin/self_test --group-name self_test_srv_grp --endpoint 0-<MAX_SERVER-1>:0 --master-endpoint 0-<MAX_SERVER-1>:0 --message-sizes "b1048576,b1048576 0,0 b1048576,i2048,i2048 0,0 i2048" --max-inflight-rpcs 16 --repetitions 100 -t -n -p .
```

**To shutdown test_group server:**
```bash
$ /usr/lib64/openmpi3/bin/orterun --mca btl self,tcp  -N 1 --hostfile clihostfile --output-filename testLogs/ -x D_LOG_FILE=testLogs/test_group_cli.log -x D_LOG_FILE_APPEND_PID=1 -x D_LOG_MASK=WARN -x CRT_PHY_ADDR_STR=ofi+sockets -x OFI_INTERFACE=eth0 -x CRT_CTX_SHARE_ADDR=0  tests/test_group_np_cli --name client-group --attach_to self_test_srv_grp --shut_only --cfg_path=.
```

## Benchmarking DAOS

DAOS can be benchmarked using several widely used IO benchmarks like IOR,
mdtest, and FIO. There are several backends that can be used with those
benchmarks.

IOR (https://github.com/hpc/ior) with the following backends:

-   POSIX, MPIIO & HDF5 drivers over dfuse and the interception library.

-   MPI-IO plugin with the ROMIO DAOS ADIO driver to bypass POSIX and dfuse. The
    MPIIO driver is available in the upstream MPICH repository.

-   HDF5 plugin with the HDF5 DAOS connector (under development). This maps the
    HDF5 data model directly to the DAOS model bypassing POSIX.

-   a custom DFS (DAOS File System) plugin integrating IOR directly with libfs
    without requiring FUSE or an interception library

-   a custom DAOS plugin integrating IOR directly with the native DAOS
    array API.

mdtest is released in the same repository as IOR. The same backends that are
listed above support mdtest, except for the MPI-IO and HDF5 backends that were
only designed to support IOR.

FIO can also be used to benchmark DAOS performance using dfuse and the
interception library with all the POSIX based engines like sync and libaio. We
do however provide a native DFS engine for FIO similar to what we do for
IOR. That engine is available on github: https://github.com/daos-stack/dfio

Finally DAOS provides a tool called daos_perf which allows benchmarking to the
DAOS object API directly or to the internal VOS API, which bypasses the client
and network stack and reports performance accessing the storage directy using
VOS.
