# Run IOR & mdtest

The `ior` benchmarking tool can be used to generate an HPC type I/O load.
This is an MPI-parallel tool that requires MPI to start and control the
IOR processes on the client nodes. For this purpose, any MPI implementation
can be used. IOR also has an MPI-IO backend, and in order to use this
MPI-IO backend with DAOS, an MPI stack that includes the DAOS ROMIO backend
has to be used to build and run IOR.
Refer to [MPI-IO Support](https://docs.daos.io/v2.6/user/mpi-io/) for details.

In addition to the default POSIX API, IOR also natively supports the
DFS backend that directly uses DAOS File System (DFS) I/O calls instead
of POSIX I/O calls. Details on the DAOS DFS backend for IOR can be found in the
[README\_DAOS](https://github.com/hpc/ior/blob/main/README_DAOS)
at the IOR github repository.

The `mdtest` tool to benchmark metadata performance is included in the
same repository as the IOR tool.
Like `IOR`, it is also an MPI-parallel application.

The [Performance Tuning](https://docs.daos.io/v2.6/admin/performance_tuning/)
section of the Administration Guide contains further information on IOR and mdtest.

## Build ior and mdtest

```sh
$ module load mpi/mpich-x86_64 # or any other MPI stack
$ git clone https://github.com/hpc/ior.git
$ cd ior
$ ./bootstrap
$ mkdir build;cd build
$ ../configure --with-daos=/usr --prefix=<your_dir>
$ make
$ make install
```

## Run ior

This example uses the default IOR `API=POSIX`, and requires that the DAOS POSIX container
is dfuse-mounted at `/tmp/daos_dfuse` on all client nodes. Refer to
[DFuse (DAOS FUSE)](https://docs.daos.io/v2.6/user/filesystem/#dfuse-daos-fuse)
for details on dfuse mounts.
The per-task performance over a dfuse mount is limited.
To obtain better performance, the POSIX API can be used in conjunction with the
[IOIL](https://docs.daos.io/v2.6/user/filesystem/#interception-library) interception library.
For best performance IOR can be run with `API=DFS`, passing in the DAOS pool and container
information (`ior -a DFS --dfs.pool=$DAOS_POOL --daos.cont=$DAOS_CONT ...`).

```sh
$ module load mpi/mpich-x86_64 # or any other MPI stack

$ mpirun -hostfile /path/to/hostfile_clients -np 30 <your_dir>/bin/ior -a POSIX -b 5G -t 1M -v -W -w -r -R -i 1 -o /tmp/daos_dfuse/testfile

IOR-3.4.0+dev: MPI Coordinated Test of Parallel I/O
Began               : Thu Apr 29 23:23:09 2021
Command line        : ior -a POSIX -b 5G -t 1M -v -W -w -r -R -i 1 -o /tmp/daos_dfuse/testfile
Machine             : Linux wolf-86.wolf.hpdd.intel.com
Start time skew across all tasks: 0.00 sec
TestID              : 0
StartTime           : Thu Apr 29 23:23:09 2021
Path                : /tmp/daos_dfuse/testfile
FS                  : 789.8 GiB   Used FS: 16.5%   Inodes: -0.0 Mi   Used Inodes: 0.0%
Participating tasks : 30

Options:
api                 : POSIX
apiVersion          :
test filename       : /tmp/daos_dfuse/testfile
access              : single-shared-file
type                : independent
segments            : 1
ordering in a file  : sequential
ordering inter file : no tasks offsets
nodes               : 3
tasks               : 30
clients per node    : 10
repetitions         : 1
xfersize            : 1 MiB
blocksize           : 5 GiB
aggregate filesize  : 150 GiB
verbose             : 1

Results:

access    bw(MiB/s)  IOPS       Latency(s)  block(KiB) xfer(KiB)  open(s)    wr/rd(s)   close(s)   total(s)   iter
------    ---------  ----       ----------  ---------- ---------  --------   --------   --------   --------   ----
Commencing write performance test: Thu Apr 29 23:23:09 2021
write     1299.23    1299.84    0.022917    5242880    1024.00    10.79      118.17     0.000377   118.22     0
Verifying contents of the file(s) just written.
Thu Apr 29 23:25:07 2021

Commencing read performance test: Thu Apr 29 23:25:35 2021

read      5429       5431       0.005523    5242880    1024.00    0.012188   28.28      0.000251   28.29      0
Max Write: 1299.23 MiB/sec (1362.35 MB/sec)
Max Read:  5429.38 MiB/sec (5693.11 MB/sec)

Summary of all tests:
Operation   Max(MiB)   Min(MiB)  Mean(MiB)     StdDev   Max(OPs)   Min(OPs)  Mean(OPs)     StdDev    Mean(s) Stonewall(s) Stonewall(MiB) Test# #Tasks tPN reps fPP reord reordoff reordrand seed segcnt   blksiz    xsize aggs(MiB)   API RefNum
write        1299.23    1299.23    1299.23       0.00    1299.23    1299.23    1299.23       0.00  118.22343         NA            NA     0     30  10    1   0     0        1         0    0      1 5368709120  1048576  153600.0 POSIX      0
read         5429.38    5429.38    5429.38       0.00    5429.38    5429.38    5429.38       0.00   28.29054         NA            NA     0     30  10    1   0     0        1         0    0      1 5368709120  1048576  153600.0 POSIX      0
Finished            : Thu Apr 29 23:26:03 2021
```

## Run mdtest

Use mdtest to create 30K files using `API=POSIX` (as for IOR, using `-a DFS` will provide much better performance):

```sh
$ mpirun -hostfile /path/to/hostfile_clients -np 10 <your_dir>/bin/mdtest -a POSIX -z 0 -F -C -i 1 -n 3334 -e 4096 -d /tmp/daos_dfuse/ -w 4096

-- started at 04/29/2021 23:28:11 --

mdtest-3.4.0+dev was launched with 10 total task(s) on 3 node(s)
Command line used: mdtest '-a' 'POSIX' '-z' '0' '-F' '-C' '-i' '1' '-n' '3334' '-e' '4096' '-d' '/tmp/daos_dfuse/' '-w' '4096'
Path: /tmp/daos_dfuse
FS: 36.5 GiB   Used FS: 18.8%   Inodes: 2.3 Mi   Used Inodes: 5.9%

Nodemap: 1001001001
10 tasks, 33340 files

SUMMARY rate: (of 1 iterations)
   Operation                      Max            Min           Mean        Std Dev
   ---------                      ---            ---           ----        -------
   File creation             :       2943.697       2943.674       2943.686          0.006
   File stat                 :          0.000          0.000          0.000          0.000
   File read                 :          0.000          0.000          0.000          0.000
   File removal              :          0.000          0.000          0.000          0.000
   Tree creation             :       1079.858       1079.858       1079.858          0.000
   Tree removal              :          0.000          0.000          0.000          0.000
-- finished at 04/29/2021 23:28:22 --
```
