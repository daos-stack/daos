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

$ cd /tmp
$ git clone https://github.com/hpc/ior.git
$ cd ior
$ git checkout 4.0.0
$ ./bootstrap
$ mkdir build; cd build
$ ../configure --with-daos=/usr --prefix=$HOME/software/ior
$ make
$ make install
```

## Run ior

This example uses the default IOR `API=POSIX`, and requires that the DAOS POSIX container
is dfuse-mounted at `/tmp/daos_dfuse` on all client nodes. Refer to
[DFuse (DAOS FUSE)](https://docs.daos.io/v2.6/user/filesystem/#dfuse-daos-fuse)
for details on dfuse mounts.

Example dfuse-mounted DAOS POSIX container:

```sh
$ dmg pool create --size=25% -u ${USER}@ test_pool
Creating DAOS pool with 25% of all storage
Pool created with 8.64%,91.36% storage tier ratio
-------------------------------------------------
  UUID                 : e0630b72-68e5-4dbc-b6ec-e1b1c201f8aa
  Service Leader       : 1
  Service Ranks        : [0-1]
  Storage Ranks        : [0-1]
  Total Size           : 860 GB
  Storage tier 0 (SCM) : 74 GB (37 GB / rank)
  Storage tier 1 (NVMe): 786 GB (393 GB / rank)

$ daos cont create test_pool test_cont --type=POSIX
Successfully created container 90e3d9a3-2eab-4f08-b912-7c468e5dce56 type POSIX
  Container UUID : 90e3d9a3-2eab-4f08-b912-7c468e5dce56
  Container Label: test_cont
  Container Type : POSIX

$ clush -B -w $CLIENT_NODES mkdir /tmp/daos_dfuse
$ clush -B -w $CLIENT_NODES dfuse /tmp/daos_dfuse test_pool test_cont
$ clush -B -w $CLIENT_NODES "df -h | grep fuse"
----------------
client-[1-2]
----------------
dfuse                                                       802G   16G  787G   2% /tmp/daos_dfuse
$
```

The per-task performance over a dfuse mount is limited.
To obtain better performance, the POSIX API can be used in conjunction with the
[IOIL](https://docs.daos.io/v2.6/user/filesystem/#interception-library) interception library.
For best performance IOR can be run with `API=DFS`, passing in the DAOS pool and container
information (`ior -a DFS --dfs.pool=$DAOS_POOL --daos.cont=$DAOS_CONT ...`).

```sh
$ module load mpi/mpich-x86_64 # or any other MPI stack

$ mpirun -hosts $CLIENT_NODES -np 10 ${HOME}/software/ior/bin/ior -a POSIX -b 2G -t 1M -v -W -w -r -R -i 1 -o /tmp/daos_dfuse/testfile

IOR-4.0.0: MPI Coordinated Test of Parallel I/O
Began               : Thu Mar 19 17:42:44 2026
Command line        : ${HOME}/software/ior/bin/ior -a POSIX -b 2G -t 1M -v -W -w -r -R -i 1 -k -o /tmp/daos_dfuse/testfile
Machine             : Linux brd-233.daos.hpc.amslabs.hpecorp.net
TestID              : 0
StartTime           : Thu Mar 19 17:42:44 2026
Path                : /tmp/daos_dfuse_test/testfile
FS                  : 801.2 GiB   Used FS: 1.9%   Inodes: -0.0 Mi   Used Inodes: 0.0%
Participating tasks : 10

Options:
api                 : POSIX
apiVersion          :
test filename       : /tmp/daos_dfuse_test/testfile
access              : single-shared-file
type                : independent
segments            : 1
ordering in a file  : sequential
ordering inter file : no tasks offsets
nodes               : 2
tasks               : 10
clients per node    : 5
repetitions         : 1
xfersize            : 1 MiB
blocksize           : 2 GiB
aggregate filesize  : 20 GiB
verbose             : 1

Results:

access    bw(MiB/s)  IOPS       Latency(s)  block(KiB) xfer(KiB)  open(s)    wr/rd(s)   close(s)   total(s)   iter
------    ---------  ----       ----------  ---------- ---------  --------   --------   --------   --------   ----
Commencing write performance test: Thu Mar 19 17:42:44 2026
write     101.04     101.04     0.098970    2097152    1024.00    0.005258   202.69     0.003326   202.70     0
Verifying contents of the file(s) just written.
Thu Mar 19 17:46:08 2026

Commencing read performance test: Thu Mar 19 17:47:54 2026

read      30912      30992      0.000321    2097152    1024.00    0.004187   0.660818   0.003185   0.662533   0
Max Write: 101.04 MiB/sec (105.95 MB/sec)
Max Read:  30911.67 MiB/sec (32413.23 MB/sec)

Summary of all tests:
Operation   Max(MiB)   Min(MiB)  Mean(MiB)     StdDev   Max(OPs)   Min(OPs)  Mean(OPs)     StdDev    Mean(s) Stonewall(s) Stonewall(MiB) Test# #Tasks tPN reps fPP reord reordoff reordrand seed segcnt   blksiz    xsize aggs(MiB)   API RefNum
write         101.04     101.04     101.04       0.00     101.04     101.04     101.04       0.00  202.69653         NA            NA     0     10   5    1   0     0        1         0    0      1 2147483648  1048576   20480.0 POSIX      0
read        30911.67   30911.67   30911.67       0.00   30911.67   30911.67   30911.67       0.00    0.66253         NA            NA     0     10   5    1   0     0        1         0    0      1 2147483648  1048576   20480.0 POSIX      0
Finished            : Thu Mar 19 17:47:55 2026
```

## Run mdtest

Use mdtest to create 30K files using `API=POSIX` (as for IOR, using `-a DFS` will provide much better performance):

```sh
$ mpirun -hosts $CLIENT_NODES -np 10 ${HOME}/software/ior/bin/mdtest -a POSIX -z 0 -F -C -i 1 -n 3334 -e 4096 -d /tmp/daos_dfuse -w 4096
-- started at 03/19/2026 17:55:06 --

mdtest-4.0.0 was launched with 10 total task(s) on 2 node(s)
Command line used: ${HOME}/software/ior/bin/mdtest '-a' 'POSIX' '-z' '0' '-F' '-C' '-i' '1' '-n' '3334' '-e' '4096' '-d' '/tmp/daos_dfuse' '-w' '4096'
Path                : /tmp/daos_dfuse_test
FS                  : 36.4 GiB   Used FS: 16.8%   Inodes: 2.3 Mi   Used Inodes: 6.8%
Nodemap: 1010101010
10 tasks, 33340 files

SUMMARY rate: (of 1 iterations)
   Operation                     Max            Min           Mean        Std Dev
   ---------                     ---            ---           ----        -------
   File creation                2568.636       2568.636       2568.636          0.000
   File stat                       0.000          0.000          0.000          0.000
   File read                       0.000          0.000          0.000          0.000
   File removal                    0.000          0.000          0.000          0.000
   Tree creation                 496.015        496.015        496.015          0.000
   Tree removal                    0.000          0.000          0.000          0.000
-- finished at 03/19/2026 17:55:20 --
```
