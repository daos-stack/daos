# DAOS Performance Tuning

This section will be expanded in a future revision.

## Network Performance

Similar to the Lustre Network stack, the DAOS CART layer can validate and
benchmark network communications in the same context as an application and using
the same networks/tuning options as regular DAOS.

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

DAOS can be benchmarked with both IOR and mdtest through the following
backends:

-   native MPI-IO plugin combined with the ROMIO DAOS ADIO driver

-   native HDF5 plugin combined with the HDF5 DAOS connector (under
    development)

-   native POSIX plugin over dfuse and interception library (under
    development)

-   a custom DFS plugin integrating mdtest & IOR directly with libfs
    without requiring FUSE or an interception library

-   a custom DAOS plugin integrating IOR directly with the native DAOS
    array API.

Moreover, a fio DAOS engine is also available.
