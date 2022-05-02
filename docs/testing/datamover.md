# DataMover

## 'daos' utility (single process)

Create Second container:

```sh
# Create Second container
$ daos container create --pool $DAOS_POOL --type POSIX --label cont2
Successfully created container 158469db-70d2-4a5d-aac9-3c06cbfa7459
```

export DAOS_CONT2=<cont uuid>

Pool Query before copy:

```sh
$ dmg pool query --pool $DAOS_POOL

Pool b22220ea-740d-46bc-84ad-35ed3a28aa31, ntarget=64, disabled=0, leader=1, version=1
Pool space info:
- Target(VOS) count:64
- SCM:
  Total size: 48 GB
  Free: 48 GB, min:743 MB, max:744 MB, mean:744 MB
- NVMe:
  Total size: 800 GB
  Free: 499 GB, min:7.7 GB, max:7.9 GB, mean:7.8 GB
Rebuild idle, 0 objs, 0 recs
```

Move data from POSIX directory into a DAOS container:

Daos 1.2 only supports directory copy if using `daos filesystem copy`

```sh
# moving everything under /tmp/daos_dfuse to new cont $DAOS_CONT2
$ daos filesystem copy --src /tmp/daos_dfuse/ --dst daos://$DAOS_POOL/$DAOS_CONT2
Successfully copied to DAOS: /
```

Pool Query to confirm data got copied (Free space has reduced from last
pool query):

```sh
dmg pool query --pool $DAOS_POOL
Pool b22220ea-740d-46bc-84ad-35ed3a28aa31, ntarget=64, disabled=0, leader=1, version=1
Pool space info:
- Target(VOS) count:64
- SCM:
  Total size: 48 GB
  Free: 47 GB, min:738 MB, max:739 MB, mean:739 MB
- NVMe:
  Total size: 800 GB
  Free: 338 GB, min:5.1 GB, max:5.5 GB, mean:5.3 GB
Rebuild idle, 0 objs, 0 recs
```

Move data from DAOS container to POSIX directory:

```sh
$ mkdir /tmp/daos_dfuse/daos_container_copy

$ daos filesystem copy --src daos://$DAOS_POOL/$DAOS_CONT2 --dst /tmp/daos_dfuse/daos_container_copy

$ mkdir /tmp/daos_dfuse/daos_cont_copy// failed, File exists
Successfully copied to POSIX: /tmp/daos_dfuse/daos_cont_copy/
```

Pool Query to confirm data got copied:

```sh
$ dmg pool query --pool $DAOS_POOL
Pool b22220ea-740d-46bc-84ad-35ed3a28aa31, ntarget=64, disabled=0, leader=1, version=1
Pool space info:
- Target(VOS) count:64
- SCM:
  Total size: 48 GB
  Free: 47 GB, min:732 MB, max:733 MB, mean:732 MB
- NVMe:
  Total size: 800 GB
  Free: 128 GB, min:1.8 GB, max:2.3 GB, mean:2.0 GB
Rebuild idle, 0 objs, 0 recs
```

Check data inside the POSIX directories:

```sh
$ ls -latr /tmp/daos_dfuse/
total 157286400
-rw-rw-r-- 1 standan standan 161061273600 Apr 29 23:23 testfile
drwxrwxr-x 1 standan standan           64 Apr 29 23:28 test-dir.0-0
drwxrwxr-x 1 standan standan           64 Apr 29 23:30 clients
drwxrwxr-x 1 standan standan           64 Apr 30 00:02 daos_container_copy

$ ls -latr /tmp/daos_dfuse/daos_container_copy
drwx------ 1 standan standan 64 Apr 30 00:02 daos_dfuse
drwx------ 1 standan standan 64 Apr 30 00:11 testfile
```

## mpifileutils (multi-process)

Build mpifileutils package:

```sh
# load mpich module or set it's path in your environment
$ module load mpi/mpich-x86_64
or
$ export LD_LIBRARY_PATH=<mpich lib path>:$LD_LIBRARY_PATH
$ export PATH=<mpich bin path>:$PATH

# install daos-devel, if missing
$ sudo yum install -y daos-devel

# Build Dependencies
$ mkdir install
$ installdir=`pwd`/install

$ mkdir deps
$ cd deps
$ wget https://github.com/hpc/libcircle/releases/download/v0.3/libcircle-0.3.0.tar.gz
$ wget https://github.com/llnl/lwgrp/releases/download/v1.0.2/lwgrp-1.0.2.tar.gz
$ wget https://github.com/llnl/dtcmp/releases/download/v1.1.0/dtcmp-1.1.0.tar.gz

$ tar -zxf libcircle-0.3.0.tar.gz
$ cd libcircle-0.3.0
$ ./configure --prefix=$installdir
$ make install
$ cd ..

$ tar -zxf lwgrp-1.0.2.tar.gz
$ cd lwgrp-1.0.2
$ ./configure --prefix=$installdir
$ make install
$ cd ..

$ tar -zxf dtcmp-1.1.0.tar.gz
$ cd dtcmp-1.1.0
$ ./configure --prefix=$installdir --with-lwgrp=$installdir
$ make install
$ cd ..
$ cd ..


# Build mpifileutils
$ git clone https://github.com/hpc/mpifileutils
$ mkdir build
$ cd build

$ cmake3 ../mpifileutils/ -DWITH_DTCMP_PREFIX=<path/to/dtcmp/install> -DWITH_LibCircle_PREFIX=<path/to/lib/circle/install> -DWITH_CART_PREFIX=/usr/ -DWITH_DAOS_PREFIX=/usr/ -DCMAKE_INSTALL_PREFIX=<path/where/mpifileutils/need/to/be/installed> -DENABLE_DAOS=ON -DENABLE_LIBARCHIVE=OFF

$ make install

# On launch node set mpifileutils LD_LIBRARY_PATH and PATH
$ export LD_LIBRARY_PATH=<mpifileutils/lib/path>:$LD_LIBRARY_PATH
$ export PATH=<mpifileutils/bin/path>:$PATH
```

Create Second container:

```sh
$ daos container create --pool $DAOS_POOL --type POSIX
Successfully created container caf0135c-def8-45a5-bac3-d0b969e67c8b

$ export DAOS_CONT2=<cont uuid>
```

Move data from POSIX directory into a DAOS container:

```sh
$ mpirun -hostfile /path/to/hostfile -np 16 /path/to/mpifileutils/install/bin/dcp --bufsize 64MB --chunksize 128MB /tmp/daos_dfuse daos://$DAOS_POOL/$DAOS_CONT2/

[2021-04-30T01:16:48] Walking /tmp/daos_dfuse
[2021-04-30T01:16:58] Walked 24207 items in 10.030 secs (2413.415 items/sec) ...
[2021-04-30T01:17:01] Walked 34245 items in 13.298 secs (2575.138 items/sec) ...
[2021-04-30T01:17:01] Walked 34245 items in 13.300 seconds (2574.867 items/sec)
[2021-04-30T01:17:01] Copying to /
[2021-04-30T01:17:01] Items: 34245
[2021-04-30T01:17:01]   Directories: 904
[2021-04-30T01:17:01]   Files: 33341
[2021-04-30T01:17:01]   Links: 0
[2021-04-30T01:17:01] Data: 150.127 GiB (4.611 MiB per file)
[2021-04-30T01:17:01] Creating 904 directories
[2021-04-30T01:17:01] Creating 33341 files.
[2021-04-30T01:17:02] Copying data.
[2021-04-30T01:17:12] Copied 4.049 GiB (3%) in 10.395 secs (398.867 MiB/s) 375 secs left ...
[2021-04-30T01:22:37] Copied 8.561 GiB (6%) in 335.113 secs (26.160 MiB/s) 5541 secs left ...
[2021-04-30T01:22:37] Copied 150.127 GiB (100%) in 335.113 secs (458.742 MiB/s) done
[2021-04-30T01:22:37] Copy data: 150.127 GiB (161197834240 bytes)
[2021-04-30T01:22:37] Copy rate: 458.741 MiB/s (161197834240 bytes in 335.114 seconds)
[2021-04-30T01:22:37] Syncing data to disk.
[2021-04-30T01:22:37] Sync completed in 0.017 seconds.
[2021-04-30T01:22:37] Fixing permissions.
[2021-04-30T01:22:37] Updated 34245 items in 0.176 seconds (194912.821 items/sec)
[2021-04-30T01:22:37] Syncing directory updates to disk.
[2021-04-30T01:22:37] Sync completed in 0.012 seconds.
[2021-04-30T01:22:37] Started: Apr-30-2021,01:17:01
[2021-04-30T01:22:37] Completed: Apr-30-2021,01:22:37
[2021-04-30T01:22:37] Seconds: 336.013
[2021-04-30T01:22:37] Items: 34245
[2021-04-30T01:22:37]   Directories: 904
[2021-04-30T01:22:37]   Files: 33341
[2021-04-30T01:22:37]   Links: 0
[2021-04-30T01:22:37] Data: 150.127 GiB (161197834240 bytes)
[2021-04-30T01:22:37] Rate: 457.513 MiB/s (161197834240 bytes in 336.013 seconds)
```

Pool Query to verify data was copied (free space should reduce):

```sh
$ dmg pool query --pool $DAOS_POOL

Pool b22220ea-740d-46bc-84ad-35ed3a28aa31, ntarget=64, disabled=0, leader=1, version=1
Pool space info:
- Target(VOS) count:64
- SCM:
  Total size: 48 GB
  Free: 47 GB, min:734 MB, max:735 MB, mean:735 MB
- NVMe:
  Total size: 800 GB
  Free: 338 GB, min:5.2 GB, max:5.4 GB, mean:5.3 GB
Rebuild idle, 0 objs, 0 recs
```

Move data from DAOS container to POSIX directory:

```sh
$ mkdir /tmp/daos_dfuse/daos_container_copy

$ mpirun -hostfile /path/to/hostfile -np 16 dcp --bufsize 64MB --chunksize 128MB daos://$DAOS_POOL/$DAOS_CONT2/ /tmp/daos_dfuse/daos_container_copy

[2021-04-30T01:26:11] Walking /
[2021-04-30T01:26:14] Walked 34246 items in 2.648 secs (12930.593 items/sec) ...
[2021-04-30T01:26:14] Walked 34246 items in 2.650 seconds (12923.056 items/sec)
[2021-04-30T01:26:14] Copying to /tmp/daos_dfuse/daos_container_copy
[2021-04-30T01:26:14] Items: 34246
[2021-04-30T01:26:14]   Directories: 905
[2021-04-30T01:26:14]   Files: 33341
[2021-04-30T01:26:14]   Links: 0
[2021-04-30T01:26:14] Data: 150.127 GiB (4.611 MiB per file)
[2021-04-30T01:26:14] Creating 905 directories
[2021-04-30T01:26:14] Original directory exists, skip the creation: `/tmp/daos_dfuse/daos_container_copy/' (errno=17 File exists)
[2021-04-30T01:26:14] Creating 33341 files.
[2021-04-30T01:26:19] Copying data.
[2021-04-30T01:26:29] Copied 3.819 GiB (3%) in 10.213 secs (382.922 MiB/s) 391 secs left ...
[2021-04-30T01:32:02] Copied 150.127 GiB (100%) in 343.861 secs (447.070 MiB/s) done
[2021-04-30T01:32:02] Copy data: 150.127 GiB (161197834240 bytes)
[2021-04-30T01:32:02] Copy rate: 447.069 MiB/s (161197834240 bytes in 343.862 seconds)
[2021-04-30T01:32:02] Syncing data to disk.
[2021-04-30T01:32:02] Sync completed in 0.020 seconds.
[2021-04-30T01:32:02] Fixing permissions.
[2021-04-30T01:32:17] Updated 34162 items in 14.955 secs (2284.295 items/sec) ...
[2021-04-30T01:32:17] Updated 34246 items in 14.955 secs (2289.890 items/sec) done
[2021-04-30T01:32:17] Updated 34246 items in 14.956 seconds (2289.772 items/sec)
[2021-04-30T01:32:17] Syncing directory updates to disk.
[2021-04-30T01:32:17] Sync completed in 0.022 seconds.
[2021-04-30T01:32:17] Started: Apr-30-2021,01:26:14
[2021-04-30T01:32:17] Completed: Apr-30-2021,01:32:17
[2021-04-30T01:32:17] Seconds: 363.327
[2021-04-30T01:32:17] Items: 34246
[2021-04-30T01:32:17]   Directories: 905
[2021-04-30T01:32:17]   Files: 33341
[2021-04-30T01:32:17]   Links: 0
[2021-04-30T01:32:17] Data: 150.127 GiB (161197834240 bytes)
[2021-04-30T01:32:17] Rate: 423.118 MiB/s (161197834240 bytes in 363.327 seconds)
```

Pool Query to very data was copied:

```sh

$ dmg pool query --pool $DAOS_POOL

Pool b22220ea-740d-46bc-84ad-35ed3a28aa31, ntarget=64, disabled=0, leader=1, version=1
Pool space info:
- Target(VOS) count:64
- SCM:
  Total size: 48 GB
  Free: 47 GB, min:728 MB, max:730 MB, mean:729 MB
- NVMe:
  Total size: 800 GB
  Free: 176 GB, min:2.6 GB, max:3.0 GB, mean:2.8 GB
Rebuild idle, 0 objs, 0 recs
```

Check data inside the POSIX directories:

```sh
$ ls -latr /tmp/daos_dfuse/

total 157286400
-rw-rw-r-- 1 standan standan 161061273600 Apr 29 23:23 testfile
drwxrwxr-x 1 standan standan           64 Apr 29 23:28 test-dir.0-0
drwxrwxr-x 1 standan standan           64 Apr 29 23:30 clients
drwxr-xr-x 1 standan standan           64 Apr 30 01:25 daos_container_copy

ls -latr /tmp/daos_dfuse/daos_container_copy
drwxr-xr-x 1 standan standan 64 Apr 30 01:26 daos_dfuse
```

For more details on datamover, reference
[DAOS Support](https://github.com/hpc/mpifileutils/DAOS-Support.md)
on the mpifileutils website.

## Clean Up

Remove one of the copy created using datamover

```sh
rm -rf /tmp/daos_dfuse/daos_container_copy
```

Remove dfuse mountpoint:

```sh
# unmount dfuse
$ pdsh -w $CLIENT_NODES 'fusermount3 -uz /tmp/daos_dfuse'

# remove mount dir
$ pdsh -w $CLIENT_NODES rm -rf /tmp/daos_dfuse
```

List containers to be destroyed:

```sh
# list containers
$ daos pool list-containers --pool $DAOS_POOL  # sample output

# sample output
cd46cf6e-f886-4682-8077-e3cbcd09b43a
caf0135c-def8-45a5-bac3-d0b969e67c8b
```

Destroy Containers:

```sh
# destroy container1
$ daos container destroy --pool $DAOS_POOL --cont $DAOS_CONT

# destroy container2
$ daos container destroy --pool $DAOS_POOL --cont $DAOS_CONT2
```

List Pools to be destroyed:

```sh
# list pool
$ dmg pool list

# sample output
Pool UUID                            Svc Replicas
---------                            ------------
b22220ea-740d-46bc-84ad-35ed3a28aa31 [1-3]
```

Destroy Pool:

```sh
# destroy pool
$ dmg pool destroy --pool $DAOS_POOL
```

Stop Agents:

```sh
# stop agents
$ pdsh -S -w $CLIENT_NODES "sudo systemctl stop daos_agent"
```

Stop Servers:

```sh
# stop servers

$ pdsh -S -w $SERVER_NODES "sudo systemctl stop daos_server"
```
