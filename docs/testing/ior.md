# Run IOR & mdtest

Use IOR to generate an HPC type POSIX I/O load to the POSIX container
and mdtest to create substantial directory structure.

## Build ior

```sh
$ git clone https://github.com/hpc/ior.git
$ cd ior
$ ./bootstrap
$ mkdir build;cd build
$ ../configure --with-daos=/usr --prefix=<your dir>
$ make
$ make install
```

Add `<your dir>/lib to LD_LIBRARY_PATh and <your dir>/bin` to PATH

## Run ior

Use ior to write and read around 150G of data

```sh
# load mpich module or set it's path in your environment
$ module load mpi/mpich-x86_64
or
$ export LD_LIBRARY_PATH=<mpich lib path>:$LD_LIBRARY_PATH
$ export PATH=<mpich bin path>:$PATH

$ mpirun -hostfile /path/to/hostfile_clients -np 30 ior -a POSIX -b 5G -t 1M -v -W -w -r -R -i 1 -k -o /tmp/daos_dfuse/testfile

IOR-3.4.0+dev: MPI Coordinated Test of Parallel I/O
Began               : Thu Apr 29 23:23:09 2021
Command line        : ior -a POSIX -b 5G -t 1M -v -W -w -r -R -i 1 -k -o /tmp/daos_dfuse/testfile
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

Use mdtest to create 30K files:

```sh
$ mpirun -hostfile /path/to/hostfile_clients -np 10 mdtest -a POSIX -z 0 -F -C -i 1 -n 3334 -e 4096 -d /tmp/daos_dfuse/ -w 4096

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
