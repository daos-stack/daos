# DAOS Tour



## Introduction

This documentation provides a general tour to the DAOS management commands
(dmg) for daos_admin, and DAOS tools (daos) for daos_client users. Help and
setup for the following is provided in this chapter:

- Pool and Container create, list, query and destroy on DAOS server for
  daos_admin and daos_client users.
- Common errors and workarounds for new users when using the dmg and daos tools.
- Example runs of data transfer between DAOS file systems, by setting up of the
  DAOS dfuse mount point and run traffic with dfuse fio and mpirun mdtest.
- Examples of basic dmg and daos tools run on 2 host DAOS servers and 1 host
  client, and runs of DAOS rebuild over dfuse fio and mpirun mdtest on a 4 host
  DAOS server.

## Requirements

Set environment variables for list of servers, client and admin node.

	# Example of 2 hosts server

	# For 1 host server, export SERVER_NODES=node-1

	export SERVER_NODES=node-1,node-2

	# Example to use admin and client on the same node

	export ADMIN_NODE=node-3

	export CLIENT_NODE=node-3

	export ALL_NODES=$SERVER_NODES,$CLIENT_NODE

## Set-Up

Refer to the [DAOS CentOS7/EL8 Setup](setup_centos7_and_el8.md) or the
[DAOS openSUSE Setup](setup_suse.md) for RPM installation, daos
server/agent/admin configuration yml files, certificate generation, and bring-up
DAOS servers and clients.

## Run with dfuse fio

### required rpm

	$ sudo yum install -y fio

### run fio

	$ dmg pool create --size 10G Pool1
	$ daos cont create --label Cont1 --type POSIX Pool1
	$ daos cont query Pool1 Cont1
	$ /usr/bin/mkdir /tmp/daos_test1
	$ /usr/bin/touch /tmp/daos_test1/testfile
	$ /usr/bin/df -h -t fuse.daos
	df: no file systems processed
	$ /usr/bin/dfuse --m=/tmp/daos_test1 --pool=Pool1 --cont=Cont1
	$ /usr/bin/df -h -t fuse.daos
	Filesystem Size Used Avail Use% Mounted on
	dfuse 954M 144K 954M 1% /tmp/daos_test1
	$ /usr/bin/fio --name=random-write --ioengine=pvsync --rw=randwrite --bs=4k --size=128M --nrfiles=4 --directory=/tmp/daos_test1 --numjobs=8 --iodepth=16 --runtime=60 --time_based --direct=1 --buffered=0 --randrepeat=0 --norandommap --refill_buffers --group_reporting
	random-write: (g=0): rw=randwrite, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=pvsync, iodepth=16
	...
	fio-3.7
	Starting 8 processes
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	write: IOPS=19.9k, BW=77.9MiB/s (81.7MB/s)(731MiB/9379msec)
	clat (usec): min=224, max=6539, avg=399.16, stdev=70.52
	lat (usec): min=224, max=6539, avg=399.19, stdev=70.52
	clat percentiles (usec):
	...
	bw ( KiB/s): min= 9368, max=10096, per=12.50%, avg=9972.06, stdev=128.28, samples=144
	iops : min= 2342, max= 2524, avg=2493.01, stdev=32.07, samples=144
	lat (usec) : 250=0.01%, 500=96.81%, 750=3.17%, 1000=0.01%
	lat (msec) : 10=0.01%
	cpu : usr=0.43%, sys=1.05%, ctx=187242, majf=0, minf=488
	IO depths : 1=100.0%, 2=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, >=64=0.0%
	submit : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=0.0%
	complete : 0=0.1%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=0.0%
	issued rwts: total=0,187022,0,0 short=0,0,0,0 dropped=0,0,0,0
	latency : target=0, window=0, percentile=100.00%, depth=16
	Run status group 0 (all jobs):
	WRITE: bw=77.9MiB/s (81.7MB/s), 77.9MiB/s-77.9MiB/s (81.7MB/s-81.7MB/s), io=731MiB (766MB), run=9379-9379msec

	# Data after fio completed
	$ ll /tmp/daos_test1
	total 1048396
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.0.0
	rw-rr- 1 user1 user1 33546240 Apr 21 23:28 random-write.0.1
	rw-rr- 1 user1 user1 33542144 Apr 21 23:28 random-write.0.2
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.0.3
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.1.0
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.1.1
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.1.2
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.1.3
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.2.0
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.2.1
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.2.2
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.2.3
	rw-rr- 1 user1 user1 33542144 Apr 21 23:28 random-write.3.0
	rw-rr- 1 user1 user1 33550336 Apr 21 23:28 random-write.3.1
	rw-rr- 1 user1 user1 33550336 Apr 21 23:28 random-write.3.2
	rw-rr- 1 user1 user1 33542144 Apr 21 23:28 random-write.3.3
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.4.0
	rw-rr- 1 user1 user1 33525760 Apr 21 23:28 random-write.4.1
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.4.2
	rw-rr- 1 user1 user1 33550336 Apr 21 23:28 random-write.4.3
	rw-rr- 1 user1 user1 33542144 Apr 21 23:28 random-write.5.0
	rw-rr- 1 user1 user1 33546240 Apr 21 23:28 random-write.5.1
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.5.2
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.5.3
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.6.0
	rw-rr- 1 user1 user1 33550336 Apr 21 23:28 random-write.6.1
	rw-rr- 1 user1 user1 33550336 Apr 21 23:28 random-write.6.2
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.6.3
	rw-rr- 1 user1 user1 33525760 Apr 21 23:28 random-write.7.0
	rw-rr- 1 user1 user1 33554432 Apr 21 23:28 random-write.7.1
	rw-rr- 1 user1 user1 33525760 Apr 21 23:28 random-write.7.2
	rw-rr- 1 user1 user1 33542144 Apr 21 23:28 random-write.7.3

### unmount

	$ fusermount -u /tmp/daos_test1/

	$ df -h -t fuse.daos
	df: no file systems processed

## Run with mpirun mdtest

### required rpms

	$ sudo yum install -y mpich
	$ sudo yum install -y ior
	$ sudo yum install -y Lmod
	$ sudo module load mpi/mpich-x86_64
	$ /usr/bin/touch /tmp/daos_test1/testfile

### run mpirun ior and mdtest

	# Run mpirun ior
    $ /usr/bin/dfuse --m=/tmp/daos_test1 --pool=Pool1 --cont=Cont1
	$ /usr/lib64/mpich/bin/mpirun -host <host1> -np 30 ior -a POSIX -b 26214400 -v -w -k -i 1 -o /tmp/daos_test1/testfile -t 25M
	IOR-3.4.0+dev: MPI Coordinated Test of Parallel I/O
	Began : Fri Apr 16 18:07:56 2021
	Command line : ior -a POSIX -b 26214400 -v -w -k -i 1 -o /tmp/daos_test1/testfile -t 25M
	Machine : Linux boro-8.boro.hpdd.intel.com
	Start time skew across all tasks: 0.00 sec
	TestID : 0
	StartTime : Fri Apr 16 18:07:56 2021
	Path : /tmp/daos_test1/testfile
	FS : 3.8 GiB Used FS: 1.1% Inodes: 0.2 Mi Used Inodes: 0.1%
	Participating tasks : 30
	Options:
	api : POSIX
	apiVersion :
	test filename : /tmp/daos_test1/testfile
	access : single-shared-file
	type : independent
	segments : 1
	ordering in a file : sequential
	ordering inter file : no tasks offsets
	nodes : 1
	tasks : 30
	clients per node : 30
	repetitions : 1
	xfersize : 25 MiB
	blocksize : 25 MiB
	aggregate filesize : 750 MiB
	verbose : 1
	Results:
	access bw(MiB/s) IOPS Latency(s) block(KiB) xfer(KiB) open(s) wr/rd(s) close(s) total(s) iter
	------ --------- ---- ---------- ---------- --------- -------- -------- -------- -------- ----
	Commencing write performance test: Fri Apr 16 18:07:56 2021
	write 1499.68 59.99 0.480781 25600 25600 0.300237 0.500064 0.483573 0.500107 0
	Max Write: 1499.68 MiB/sec (1572.53 MB/sec)
	Summary of all tests:
	Operation Max(MiB) Min(MiB) Mean(MiB) StdDev Max(OPs) Min(OPs) Mean(OPs) StdDev Mean(s) Stonewall(s) Stonewall(MiB) Test# #Tasks tPN reps fPP reord reordoff reordrand seed segcnt blksiz xsize aggs(MiB) API RefNum
	write 1499.68 1499.68 1499.68 0.00 59.99 59.99 59.99 0.00 0.50011 NA NA 0 30 30 1 0 0 1 0 0 1 26214400 26214400 750.0 POSIX 0
	Finished : Fri Apr 16 18:07:57 2021


### Run mpirun mdtest

	$ /usr/lib64/mpich/bin/mpirun -host <host1> -np 30 mdtest -a DFS -z 0 -F -C -i 1 -n 1667 -e 4096 -d / -w 4096 --dfs.chunk_size 1048576 --dfs.cont <container.uuid> --dfs.destroy --dfs.dir_oclass RP_3G1 --dfs.group daos_server --dfs.oclass RP_3G1 --dfs.pool <pool_uuid>
	– started at 04/16/2021 22:01:55 –
	mdtest-3.4.0+dev was launched with 30 total task(s) on 1 node(s)
	Command line used: mdtest 'a' 'DFS' '-z' '0' '-F' '-C' '-i' '1' '-n' '1667' '-e' '4096' '-d' '/' '-w' '4096' 'dfs.chunk_size' '1048576' 'dfs.cont' '3e661024-2f1f-4d7a-9cd4-1b05601e0789' 'dfs.destroy' 'dfs.dir_oclass' 'SX' 'dfs.group' 'daos_server' 'dfs.oclass' 'SX' '-dfs.pool' 'd546a7f5-586c-4d8f-aecd-372878df7b97'
	WARNING: unable to use realpath() on file system.
	Path:
	FS: 0.0 GiB Used FS: -nan% Inodes: 0.0 Mi Used Inodes: -nan%
	Nodemap: 111111111111111111111111111111
	30 tasks, 50010 files
	SUMMARY rate: (of 1 iterations)
	Operation Max Min Mean Std Dev
	--------- — — ---- -------
	File creation : 14206.584 14206.334 14206.511 0.072
	File stat : 0.000 0.000 0.000 0.000
	File read : 0.000 0.000 0.000 0.000
	File removal : 0.000 0.000 0.000 0.000
	Tree creation : 1869.791 1869.791 1869.791 0.000
	Tree removal : 0.000 0.000 0.000 0.000
	– finished at 04/16/2021 22:01:58 –

	$ /usr/lib64/mpich/bin/mpirun -host <host1> -np 50 mdtest -a DFS -z 0 -F -C -i 1 -n 1667 -e 4096 -d / -w 4096 --dfs.chunk_size 1048576 --dfs.cont 3e661024-2f1f-4d7a-9cd4-1b05601e0789 --dfs.destroy --dfs.dir_oclass SX --dfs.group daos_server --dfs.oclass SX --dfs.pool d546a7f5-586c-4d8f-aecd-372878df7b97
	– started at 04/16/2021 22:02:21 –
	mdtest-3.4.0+dev was launched with 50 total task(s) on 1 node(s)
	Command line used: mdtest 'a' 'DFS' '-z' '0' '-F' '-C' '-i' '1' '-n' '1667' '-e' '4096' '-d' '/' '-w' '4096' 'dfs.chunk_size' '1048576' 'dfs.cont' '3e661024-2f1f-4d7a-9cd4-1b05601e0789' 'dfs.destroy' 'dfs.dir_oclass' 'SX' 'dfs.group' 'daos_server' 'dfs.oclass' 'SX' '-dfs.pool' 'd546a7f5-586c-4d8f-aecd-372878df7b97'
	WARNING: unable to use realpath() on file system.
	Path:
	FS: 0.0 GiB Used FS: -nan% Inodes: 0.0 Mi Used Inodes: -nan%
	Nodemap: 11111111111111111111111111111111111111111111111111
	50 tasks, 83350 files
	SUMMARY rate: (of 1 iterations)
	Operation Max Min Mean Std Dev
	--------- — — ---- -------
	File creation : 13342.303 13342.093 13342.228 0.059
	File stat : 0.000 0.000 0.000 0.000
	File read : 0.000 0.000 0.000 0.000
	File removal : 0.000 0.000 0.000 0.000
	Tree creation : 1782.938 1782.938 1782.938 0.000
	Tree removal : 0.000 0.000 0.000 0.000
	– finished at 04/16/2021 22:02:27 –

## Run with 4 DAOS hosts server, rebuild with dfuse io and mpirun

### Environment variables setup

	export SERVER_NODES=node-1,node-2,node-3,node-4
	export ADMIN_NODE=node-5
	export CLIENT_NODE=node-5
	export ALL_NODES=$SERVER_NODES,$CLIENT_NODE

### Run dfuse

	# Bring up 4 hosts server with appropriate daos_server.yml and
	# access-point, reference to  DAOS Set-Up
	# After DAOS servers, DAOS admin and client started.

	$ dmg storage format
	Format Summary:
	  Hosts             SCM Devices NVMe Devices
	  -----             ----------- ------------
	  boro-[8,35,52-53] 1           0

	$ dmg pool create --size 10G Pool1
	$ daos cont create --label Cont1 --type POSIX --oclass RP_3G1 --properties rf:2 Pool1  
	$ daos pool list-cont Pool1
	UUID                                 Label
	----                                 ----- 
	2649aa0f-3ad7-4943-abf5-4343205a637b Cont1

	$ dmg pool query Pool1
	Pool 733bee7b-c2af-499e-99dd-313b1ef092a9, ntarget=32, disabled=0, leader=2, version=1
	Pool space info:
	- Target(VOS) count:32
	- SCM:
	  Total size: 5.0 GB
	  Free: 5.0 GB, min:156 MB, max:156 MB, mean:156 MB
	- NVMe:
	  Total size: 0 B
	  Free: 0 B, min:0 B, max:0 B, mean:0 B
	Rebuild idle, 0 objs, 0 recs

	$ df -h -t fuse.daos
	df: no file systems processed

	$ mkdir /tmp/daos_test1

	$ dfuse --mountpoint=/tmp/daos_test1 --pool=Pool1 --cont=Cont1

	$ df -h -t fuse.daos
	Filesystem      Size  Used Avail Use% Mounted on
	dfuse            19G  1.1M   19G   1% /tmp/daos_test1

	$ fio --name=random-write --ioengine=pvsync --rw=randwrite --bs=4k --size=128M --nrfiles=4 --directory=/tmp/daos_test1 --numjobs=8 --iodepth=16 --runtime=60 --time_based --direct=1 --buffered=0 --randrepeat=0 --norandommap --refill_buffers --group_reporting
	random-write: (g=0): rw=randwrite, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=pvsync, iodepth=16
	...
	fio-3.7
	Starting 8 processes
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	random-write: Laying out IO files (4 files / total 128MiB)
	Jobs: 8 (f=32): [w(8)][100.0%][r=0KiB/s,w=96.1MiB/s][r=0,w=24.6k IOPS][eta 00m:00s]
	random-write: (groupid=0, jobs=8): err= 0: pid=27879: Sat Apr 17 01:12:57 2021
	  write: IOPS=24.4k, BW=95.3MiB/s (99.9MB/s)(5716MiB/60001msec)
		clat (usec): min=220, max=6687, avg=326.19, stdev=55.29
		 lat (usec): min=220, max=6687, avg=326.28, stdev=55.29
		clat percentiles (usec):
		 |  1.00th=[  260],  5.00th=[  273], 10.00th=[  285], 20.00th=[  293],
		 | 30.00th=[  306], 40.00th=[  314], 50.00th=[  322], 60.00th=[  330],
		 | 70.00th=[  338], 80.00th=[  355], 90.00th=[  375], 95.00th=[  396],
		 | 99.00th=[  445], 99.50th=[  465], 99.90th=[  523], 99.95th=[  562],
		 | 99.99th=[ 1827]
	   bw (  KiB/s): min=10976, max=12496, per=12.50%, avg=12191.82, stdev=157.87, samples=952
	   iops        : min= 2744, max= 3124, avg=3047.92, stdev=39.47, samples=952
	  lat (usec)   : 250=0.23%, 500=99.61%, 750=0.15%, 1000=0.01%
	  lat (msec)   : 2=0.01%, 4=0.01%, 10=0.01%
	  cpu          : usr=0.81%, sys=1.69%, ctx=1463535, majf=0, minf=308
	  IO depths    : 1=100.0%, 2=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, >=64=0.0%
		 submit    : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=0.0%
		 complete  : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=0.0%
		 issued rwts: total=0,1463226,0,0 short=0,0,0,0 dropped=0,0,0,0
		 latency   : target=0, window=0, percentile=100.00%, depth=16

	Run status group 0 (all jobs):
	  WRITE: bw=95.3MiB/s (99.9MB/s), 95.3MiB/s-95.3MiB/s (99.9MB/s-99.9MB/s), io=5716MiB (5993MB), run=60001-60001msec

### Run dfuse with rebuild

	# Start dfuse
	$ fio --name=random-write --ioengine=pvsync --rw=randwrite --bs=4k --size=128M --nrfiles=4 --directory=/tmp/daos_test1 --numjobs=8 --iodepth=16 --runtime=60 --time_based --direct=1 --buffered=0 --randrepeat=0 --norandommap --refill_buffers --group_reporting

	random-write: (g=0): rw=randwrite, bs=(R) 4096B-4096B, (W) 4096B-4096B, (T) 4096B-4096B, ioengine=pvsync, iodepth=16
	...
	fio-3.7
	Starting 8 processes
	fio: io_u error on file /tmp/daos_test1/random-write.2.1: Input/output error: write offset=8527872, buflen=4096
	fio: pid=28242, err=5

	file:io_u.c:1747
	bw ( KiB/s): min= 3272, max=12384, per=30.14%, avg=11624.50, stdev=2181.01, samples=128
	iops : min= 818, max= 3096, avg=2906.12, stdev=545.25, samples=128
	lat (usec) : 250=0.23%, 500=99.59%, 750=0.12%, 1000=0.01%
	lat (msec) : 2=0.03%, 4=0.02%
	cpu : usr=0.27%, sys=0.66%, ctx=186210, majf=0, minf=494
	IO depths : 1=100.0%, 2=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, >=64=0.0%
	submit : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=0.0%
	complete : 0=0.1%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=0.0%
	issued rwts: total=0,186000,0,0 short=0,0,0,0 dropped=0,0,0,0
	latency : target=0, window=0, percentile=100.00%, depth=16
	Run status group 0 (all jobs):
	WRITE: bw=37.7MiB/s (39.5MB/s), 37.7MiB/s-37.7MiB/s (39.5MB/s-39.5MB/s), io=727MiB (762MB), run=19291-19291msec
	...
	# from daos_admin console, stop leader-rank with debug
	$ dmg -d system stop --ranks=3
	DEBUG 01:34:58.026753 main.go:217: debug output enabled
	DEBUG 01:34:58.027457 main.go:244: control config loaded from /etc/daos/daos_control.yml
	Rank Operation Result
	--------- ------
	3 stop OK

	$ daos pool list-cont Pool1
	cf2a95ce-9910-4d5e-814c-cafb0a7f0944

	$ dmg pool query Pool1
	Pool 70f73efc-848e-4f6e-b4fd-909bcf9bd427,
	ntarget=32,
	disabled=8,
	leader=2,
	version=18
	Pool space info:
	Target(VOS) count:24
	SCM:
	Total size: 15 GB
	Free: 14 GB, min:575 MB, max:597 MB, mean:587 MB
	NVMe:
	Total size: 0 B
	Free: 0 B, min:0 B, max:0 B, mean:0 B
	Rebuild done, 1 objs, 57 recs

	# Verify stopped server been evicted
	$ dmg system query -v
	Rank UUID Control Address Fault Domain State Reason
	---- --------------- ------------ ----- ------
	0 2bf0e083-33d6-4ce3-83c4-c898c2a7ddbd 10.7.1.8:10001 boro-8.boro.hpdd.intel.com Joined
	1 c9ac1dd9-0f9d-4684-90d3-038b720fd26b 10.7.1.35:10001 boro-35.boro.hpdd.intel.com Joined
	2 80e44fe9-3a2b-4808-9a0f-88c3cbe7f565 10.7.1.53:10001 boro-53.boro.hpdd.intel.com Joined
	3 a26fd44a-6089-4cc3-a06b-278a85607fd3 10.7.1.52:10001 boro-52.boro.hpdd.intel.com Evicted system stop

	# Restart, after evicted server restarted, verify the server joined
	$ /usr/bin/dmg system query -v
	Rank UUID Control Address Fault Domain State Reason
	---- --------------- ------------ ----- ------
	0 2bf0e083-33d6-4ce3-83c4-c898c2a7ddbd 10.7.1.8:10001 /boro-8.boro.hpdd.intel.com Joined
	1 c9ac1dd9-0f9d-4684-90d3-038b720fd26b 10.7.1.35:10001 /boro-35.boro.hpdd.intel.com Joined
	2 80e44fe9-3a2b-4808-9a0f-88c3cbe7f565 10.7.1.53:10001 /boro-53.boro.hpdd.intel.com Joined
	3 a26fd44a-6089-4cc3-a06b-278a85607fd3 10.7.1.52:10001 /boro-52.boro.hpdd.intel.com Joined

	# Unmount after test completed
	$ fusermount -u /tmp/daos_test1/
	$ df -h -t fuse.daos
	df: no file systems processed

### Run mpirun mdtest with rebuild

	$ dmg pool create --size=50G Pool1
	Creating DAOS pool with automatic storage allocation: 50 GB NVMe + 6.00% SCM
	Pool created with 100.00% SCM/NVMe ratio
	-----------------------------------------
	 UUID : 4eda8a8c-028c-461c-afd3-704534961572
	 Service Ranks : [1-3]
	 Storage Ranks : [0-3]
	 Total Size : 50 GB
	 SCM : 50 GB (12 GB / rank)
	 NVMe : 0 B (0 B / rank)

	$ daos cont create --label Cont1 --type POSIX --oclass RP_3G1 --properties rf:2 Pool1  
	Successfully created container d71ff6a5-15a5-43fe-b829-bef9c65b9ccb

	$ /usr/lib64/mpich/bin/mpirun -host boro-8 -np 30 mdtest -a DFS -z 0 -F -C -i 100 -n 1667 -e 4096 -d / -w 4096 --dfs.chunk_size 1048576 --dfs.cont Cont1 --dfs.destroy --dfs.dir_oclass RP_3G1 --dfs.group daos_server --dfs.oclass RP_3G1 --dfs.pool Pool1

	started at 04/22/2021 17:46:20 –
	mdtest-3.4.0+dev was launched with 30 total task(s) on 1 node(s)
	Command line used: mdtest 'a' 'DFS' '-z' '0' '-F' '-C' '-i' '100' '-n' '1667' '-e' '4096' '-d' '/' '-w' '4096' 'dfs.chunk_size' '1048576' 'dfs.cont' 'd71ff6a5-15a5-43fe-b829-bef9c65b9ccb' 'dfs.destroy' 'dfs.dir_oclass' 'RP_3G1' 'dfs.group' 'daos_server' 'dfs.oclass' 'RP_3G1' '-dfs.pool' '4eda8a8c-028c-461c-afd3-704534961572'
	WARNING: unable to use realpath() on file system.
	Path:
	FS: 0.0 GiB Used FS: -nan% Inodes: 0.0 Mi Used Inodes: -nan%
	Nodemap: 111111111111111111111111111111
	30 tasks, 50010 files
	...

	# from daos_admin console, stop a server rank
	$ dmg system stop --ranks=2
	Rank Operation Result
	--------- ------
	2 stop OK

	# Verify stopped server been evicted
	$ dmg system query -v
	Rank UUID Control Address Fault Domain State Reason
	 ---- --------------- ------------ ----- ------
	 0 2bf0e083-33d6-4ce3-83c4-c898c2a7ddbd 10.7.1.8:10001 boro-8.boro.hpdd.intel.com Joined
	 1 c9ac1dd9-0f9d-4684-90d3-038b720fd26b 10.7.1.35:10001 boro-35.boro.hpdd.intel.com Joined
	 2 80e44fe9-3a2b-4808-9a0f-88c3cbe7f565 10.7.1.53:10001 boro-53.boro.hpdd.intel.com Evicted system stop
	 3 a26fd44a-6089-4cc3-a06b-278a85607fd3 10.7.1.52:10001 boro-52.boro.hpdd.intel.com Joined

	 # Restart, after evicted server restarted, verify the server joined
	$ /usr/bin/dmg system query -v
	 Rank UUID Control Address Fault Domain State Reason
	 ---- --------------- ------------ ----- ------
	 0 2bf0e083-33d6-4ce3-83c4-c898c2a7ddbd 10.7.1.8:10001 /boro-8.boro.hpdd.intel.com Joined
	 1 c9ac1dd9-0f9d-4684-90d3-038b720fd26b 10.7.1.35:10001 /boro-35.boro.hpdd.intel.com Joined
	 2 80e44fe9-3a2b-4808-9a0f-88c3cbe7f565 10.7.1.53:10001 /boro-53.boro.hpdd.intel.com Joined
	 3 a26fd44a-6089-4cc3-a06b-278a85607fd3 10.7.1.52:10001 /boro-52.boro.hpdd.intel.com Joined

## Clean-Up

	# pool reintegrate
	$ dmg pool reintegrate Pool1 --rank=2
	Reintegration command succeeded

	# destroy container
	$ daos container destroy Pool1 Cont1

	# destroy pool
	$ dmg pool destroy Pool1
	Pool-destroy command succeeded

	# stop clients
	$ pdsh -S -w $CLIENT_NODES "sudo systemctl stop daos_agent.service"

	# disable clients
	$ pdsh -S -w $CLIENT_NODES "sudo systemctl disable daos_agent.service"

	# stop servers
	$ pdsh -S -w $SERVER_NODES "sudo systemctl stop daos_server.service"

	# disable servers
	$ pdsh -S -w $SERVER_NODES "sudo systemctl disable daos_server.service"
