# DataMover


!!! note
  The example below makes use of a DAOS pool and its POSIX directory setup as part of
  [Run IOR and mdtest](https://docs.daos.io/v2.6/testing/ior/). It also makes use of data
  written to the same pool as part of [Run dbench](https://docs.daos.io/v2.6/testing/dbench/).

## 'daos' utility (single process)

Use the `daos filesystem copy` command to copy data between containers.


1. Create a new container:

```sh
$ daos cont create test_pool test_cont2 --type=POSIX
Successfully created container cec2bb35-dc65-4397-9672-8ef2607e31ea type POSIX
  Container UUID : cec2bb35-dc65-4397-9672-8ef2607e31ea
  Container Label: test_cont2
  Container Type : POSIX
```


2. Query the pool before copying data:

```sh
$ dmg pool query test_pool
Pool e0630b72-68e5-4dbc-b6ec-e1b1c201f8aa, ntarget=24, disabled=0, leader=1, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
- Data redundancy: normal
Pool space info:
- Target count:24
- Storage tier 0 (SCM):
  Total size: 74 GB
  Free: 59 GB, min:2.4 GB, max:2.5 GB, mean:2.5 GB
- Storage tier 1 (NVME):
  Total size: 786 GB
  Free: 742 GB, min:31 GB, max:33 GB, mean:31 GB
```


3. Copy data from POSIX directory into the new DAOS container:

```sh
$ daos filesystem copy --src /tmp/daos_dfuse/ --dst daos://test_pool/test_cont2
Successfully copied to DAOS: daos://test_pool/test_cont2
    Directories: 554
    Files:       33341
    Links:       0
```

!!! note
  The `daos://test_pool/test_cont2` destination path utilizes the `test_pool` pool and `test_cont`
  container labels


4. Query the pool to confirm the data copy (Free space has been reduced from last
pool query):

```sh
$ dmg pool query test_pool
Pool e0630b72-68e5-4dbc-b6ec-e1b1c201f8aa, ntarget=24, disabled=0, leader=1, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
- Data redundancy: normal
Pool space info:
- Target count:24
- Storage tier 0 (SCM):
  Total size: 74 GB
  Free: 59 GB, min:2.4 GB, max:2.5 GB, mean:2.5 GB
- Storage tier 1 (NVME):
  Total size: 786 GB
  Free: 698 GB, min:29 GB, max:31 GB, mean:29 GB
```


5. Copy data from the new DAOS container to POSIX directory:

```sh
$ mkdir /tmp/daos_dfuse/daos_container_copy
$ daos filesystem copy --src daos://test_pool/test_cont2 --dst /tmp/daos_dfuse/daos_container_copy
Successfully copied to DAOS: /tmp/daos_dfuse/daos_container_copy
    Directories: 554
    Files:       33341
    Links:       0
```


6. Query the pool to confirm data copy (Free space has been further reduced from last
pool query):

```sh
$ dmg pool query test_pool
Pool e0630b72-68e5-4dbc-b6ec-e1b1c201f8aa, ntarget=24, disabled=0, leader=1, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
- Data redundancy: normal
Pool space info:
- Target count:24
- Storage tier 0 (SCM):
  Total size: 74 GB
  Free: 59 GB, min:2.4 GB, max:2.5 GB, mean:2.5 GB
- Storage tier 1 (NVME):
  Total size: 786 GB
  Free: 655 GB, min:27 GB, max:29 GB, mean:27 GB
```


7. Check data inside the POSIX directories:

```sh
$ ls -latr /tmp/daos_dfuse/
total 20971520
-rw-r--r-- 1 hendersp ldap 21474836480 Mar 19 17:46 testfile
drwxr-xr-x 1 hendersp ldap         104 Mar 19 17:55 test-dir.0-0
drwxr-xr-x 1 hendersp ldap         104 Mar 19 21:50 clients
drwxr-xr-x 1 hendersp ldap         104 Mar 19 22:48 daos_container_copy
$
$ ls -latr /tmp/daos_dfuse/daos_container_copy
drwxr-xr-x 1 hendersp ldap         104 Mar 19 22:37 clients
-rw-r--r-- 1 hendersp ldap 21474836480 Mar 19 22:46 testfile
drwxr-xr-x 1 hendersp ldap         104 Mar 19 22:48 test-dir.0-0
```

## mpifileutils (multi-process)

Use `mpifileutils` to move data between containers.

1. Build mpifileutils package:

Load mpich module or set it's path in your environment

```sh
$ module load mpi/mpich
$ module list
Currently Loaded Modulefiles:
 1) mpich/5.0.0b1.lua
```
or
```sh
$ export LD_LIBRARY_PATH=<mpich lib path>:$LD_LIBRARY_PATH
$ export PATH=<mpich bin path>:$PATH
```

Install dependencies

For RHEL:
```sh
$ sudo dnf install -y daos-devel libattr-devel bzip2-devel
```
For SLES:
```sh
$ sudo zypper install -y daos-devel libattr-devel libbz2-devel
```

Build other dependencies
```sh
$ installdir=${HOME}/software/mpifileutils
$ mkdir $installdir

$ cd /tmp
$ mkdir deps; cd deps
$ wget https://github.com/hpc/libcircle/releases/download/v0.3/libcircle-0.3.0.tar.gz
$ wget https://github.com/llnl/lwgrp/releases/download/v1.0.4/lwgrp-1.0.4.tar.gz
$ wget https://github.com/llnl/dtcmp/releases/download/v1.1.4/dtcmp-1.1.4.tar.gz
$ wget https://github.com/libarchive/libarchive/releases/download/v3.5.1/libarchive-3.5.1.tar.gz

$ tar -zxf libcircle-0.3.0.tar.gz
$ cd libcircle-0.3.0
$ ./configure --prefix=$installdir
$ make install
$ cd ..

$ tar -zxf lwgrp-1.0.4.tar.gz
$ cd lwgrp-1.0.4
$ ./configure --prefix=$installdir
$ make install
$ cd ..

$ tar -zxf dtcmp-1.1.4.tar.gz
$ cd dtcmp-1.1.4
$ export LD_LIBRARY_PATH=$installdir/lib64:$LD_LIBRARY_PATH
$ ./configure --prefix=$installdir --with-lwgrp=$installdir
$ make install
$ cd ..

$ tar -zxf libarchive-3.5.1.tar.gz
$ cd libarchive-3.5.1/
$ ./configure --prefix=$installdir
$ make install
$ cd ..
```

Build mpifileutils

```sh
$ wget https://github.com/hpc/mpifileutils/releases/download/v0.11.1/mpifileutils-v0.11.1.tgz
$ tar -zxf mpifileutils-v0.11.1.tgz
$ cd mpifileutils-v0.11.1/
$ mkdir build; cd build
$ cmake .. -DWITH_LibArchive_PREFIX=$installdir -DWITH_DTCMP_PREFIX=$installdir -DWITH_LibCircle_PREFIX=$installdir -DCMAKE_INSTALL_PREFIX=$installdir -DWITH_DAOS_PREFIX=/usr/ -DENABLE_DAOS=ON -DMPI_C_COMPILER=${HOME}/software/mpich/bin/mpicc -DMPI_CXX_COMPILER=${HOME}/software/mpich/bin/mpicxx
$ make -j install
$ cd

$ export LD_LIBRARY_PATH=${HOME}/software/mpifileutils/lib64/:$LD_LIBRARY_PATH
$ export PATH=${HOME}/software/mpifileutils/bin/:$PATH
$ which dcp
~/software/mpifileutils/bin/dcp
```


2. Create a new container:

```sh
$ daos cont create test_pool test_cont3 --type=POSIX
Successfully created container 86e9c9a3-8667-4ad4-9a0c-364a318acb6b type POSIX
  Container UUID : 86e9c9a3-8667-4ad4-9a0c-364a318acb6b
  Container Label: test_cont3
  Container Type : POSIX
```


3. Query the pool before moving data:

```sh
$ dmg pool query test_pool
Pool e0630b72-68e5-4dbc-b6ec-e1b1c201f8aa, ntarget=24, disabled=0, leader=1, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
- Data redundancy: normal
Pool space info:
- Target count:24
- Storage tier 0 (SCM):
  Total size: 74 GB
  Free: 59 GB, min:2.4 GB, max:2.5 GB, mean:2.5 GB
- Storage tier 1 (NVME):
  Total size: 786 GB
  Free: 655 GB, min:27 GB, max:29 GB, mean:27 GB
```


4. Move data from POSIX directory into the new DAOS container:

```sh
$ mpirun -hosts $CLIENT_NODES -np 16 -wdir /tmp/daos_dfuse ${HOME}/software/mpifileutils/bin/dcp --bufsize 64MB --chunksize 128MB /tmp/daos_dfuse daos://test_pool/test_cont3/
[2026-03-20T18:01:05] Walking /
[2026-03-20T18:01:09] Walked 66910 items in 2.577 secs (25964.570 items/sec) ...
[2026-03-20T18:01:09] Walked 66910 items in 2.582 seconds (25914.574 items/sec)
[2026-03-20T18:01:09] Copying to /
[2026-03-20T18:01:09] Items: 66910
[2026-03-20T18:01:09]   Directories: 228
[2026-03-20T18:01:09]   Files: 66682
[2026-03-20T18:01:09]   Links: 0
[2026-03-20T18:01:09] Data: 40.254 GiB (633.000 KiB per file)
[2026-03-20T18:01:09] Creating 228 directories
[2026-03-20T18:01:09] Original directory exists, skip the creation: `//' (errno=17 File exists)
[2026-03-20T18:01:09] Creating 66682 files.
[2026-03-20T18:01:12] Copying data.
[2026-03-20T18:01:27] Copied 363.957 MiB (1%) in 15.000 secs (24.264 MiB/s) 1684 secs left ...
[2026-03-20T18:19:46] Copied 810.250 MiB (2%) in 1114.103 secs (744.721 KiB/s) 55564 secs left ...
[2026-03-20T18:19:46] Copied 40.254 GiB (100%) in 1114.103 secs (36.999 MiB/s) done
[2026-03-20T18:19:46] Copy data: 40.254 GiB (43222794240 bytes)
[2026-03-20T18:19:46] Copy rate: 36.999 MiB/s (43222794240 bytes in 1114.103 seconds)
[2026-03-20T18:19:46] Syncing data to disk.
[2026-03-20T18:19:46] Sync completed in 0.001 seconds.
[2026-03-20T18:19:46] Fixing permissions.
[2026-03-20T18:19:53] Updated 67790 items in 6.090 seconds (11130.953 items/sec)
[2026-03-20T18:19:53] Syncing directory updates to disk.
[2026-03-20T18:19:53] Sync completed in 0.001 seconds.
[2026-03-20T18:19:53] Started: Mar-20-2026,18:01:03
[2026-03-20T18:19:53] Completed: Mar-20-2026,18:19:53
[2026-03-20T18:19:53] Seconds: 1129.781
[2026-03-20T18:19:53] Items: 67790
[2026-03-20T18:19:53]   Directories: 1108
[2026-03-20T18:19:53]   Files: 66682
[2026-03-20T18:19:53]   Links: 0
[2026-03-20T18:19:53] Data: 40.254 GiB (43222794240 bytes)
[2026-03-20T18:19:53] Rate: 36.485 MiB/s (43222794240 bytes in 1129.781 seconds)
```


5. Query the pool to verify data was copied (free space has been reduced):

```sh
$ dmg pool query test_pool
Pool e0630b72-68e5-4dbc-b6ec-e1b1c201f8aa, ntarget=24, disabled=0, leader=1, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
- Data redundancy: normal
Pool space info:
- Target count:24
- Storage tier 0 (SCM):
  Total size: 74 GB
  Free: 59 GB, min:2.4 GB, max:2.5 GB, mean:2.4 GB
- Storage tier 1 (NVME):
  Total size: 786 GB
  Free: 569 GB, min:23 GB, max:27 GB, mean:24 GB
```


6. Move data from the new DAOS container to POSIX directory:

```sh
$ mkdir /tmp/daos_dfuse/test_cont3_copy
$ mpirun -hosts $CLIENT_NODES -np 16 -wdir /tmp/daos_dfuse ${HOME}/software/mpifileutils/bin/dcp --bufsize 64MB --chunksize 128MB daos://test_pool/test_cont3/ /tmp/daos_dfuse/test_cont3_copy
[2026-03-20T18:31:28] Walking /
[2026-03-20T18:31:31] Walked 67790 items in 2.685 secs (25248.913 items/sec) ...
[2026-03-20T18:31:31] Walked 67790 items in 2.686 seconds (25241.379 items/sec)
[2026-03-20T18:31:31] Copying to /test_cont3_copy
[2026-03-20T18:31:31] Items: 67790
[2026-03-20T18:31:31]   Directories: 1108
[2026-03-20T18:31:31]   Files: 66682
[2026-03-20T18:31:31]   Links: 0
[2026-03-20T18:31:31] Data: 40.254 GiB (633.000 KiB per file)
[2026-03-20T18:31:31] Creating 1108 directories
[2026-03-20T18:31:31] Original directory exists, skip the creation: `/test_cont3_copy/' (errno=17 File exists)
[2026-03-20T18:31:31] Creating 66682 files.
[2026-03-20T18:31:37] Copying data.
[2026-03-20T18:31:52] Copied 305.402 MiB (1%) in 15.012 secs (20.345 MiB/s) 2011 secs left ...
[2026-03-20T18:49:34] Copied 752.625 MiB (2%) in 1076.404 secs (715.984 KiB/s) 57877 secs left ...
[2026-03-20T18:49:34] Copied 40.254 GiB (100%) in 1076.404 secs (38.295 MiB/s) done
[2026-03-20T18:49:34] Copy data: 40.254 GiB (43222794240 bytes)
[2026-03-20T18:49:34] Copy rate: 38.295 MiB/s (43222794240 bytes in 1076.404 seconds)
[2026-03-20T18:49:34] Syncing data to disk.
[2026-03-20T18:49:34] Sync completed in 0.001 seconds.
[2026-03-20T18:49:34] Fixing permissions.
[2026-03-20T18:49:40] Updated 67790 items in 6.317 seconds (10731.073 items/sec)
[2026-03-20T18:49:40] Syncing directory updates to disk.
[2026-03-20T18:49:40] Sync completed in 0.001 seconds.
[2026-03-20T18:49:40] Started: Mar-20-2026,18:31:31
[2026-03-20T18:49:40] Completed: Mar-20-2026,18:49:40
[2026-03-20T18:49:40] Seconds: 1089.640
[2026-03-20T18:49:40] Items: 67790
[2026-03-20T18:49:40]   Directories: 1108
[2026-03-20T18:49:40]   Files: 66682
[2026-03-20T18:49:40]   Links: 0
[2026-03-20T18:49:40] Data: 40.254 GiB (43222794240 bytes)
[2026-03-20T18:49:40] Rate: 37.829 MiB/s (43222794240 bytes in 1089.640 seconds)
```


7. Query the pool to very data was copied (free space has been further reduced):

```sh
$ dmg pool query test_pool
Pool e0630b72-68e5-4dbc-b6ec-e1b1c201f8aa, ntarget=24, disabled=0, leader=1, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
- Data redundancy: normal
Pool space info:
- Target count:24
- Storage tier 0 (SCM):
  Total size: 74 GB
  Free: 58 GB, min:2.4 GB, max:2.5 GB, mean:2.4 GB
- Storage tier 1 (NVME):
  Total size: 786 GB
  Free: 482 GB, min:19 GB, max:23 GB, mean:20 GB
```


8. Check data inside the POSIX directories:

```sh
$ ls -latr /tmp/daos_dfuse/
total 20971520
-rw-r--r-- 1 hendersp ldap 21474836480 Mar 19 17:46 testfile
drwxr-xr-x 1 hendersp ldap         104 Mar 19 17:55 test-dir.0-0
drwxr-xr-x 1 hendersp ldap         104 Mar 19 21:50 clients
drwxr-xr-x 1 hendersp ldap         104 Mar 19 22:48 daos_container_copy
drwxr-xr-x 1 hendersp ldap         104 Mar 20 18:49 test_cont3_copy
$
$ ls -latr /tmp/daos_dfuse/test_cont3_copy
total 20971520
-rw-r--r-- 1 hendersp ldap 21474836480 Mar 20 18:49 testfile
drwxr-xr-x 1 hendersp ldap         104 Mar 20 18:49 daos_container_copy
drwxr-xr-x 1 hendersp ldap         104 Mar 20 18:49 test-dir.0-0
drwxr-xr-x 1 hendersp ldap         104 Mar 20 18:49 clients
$
$ ls -latr /tmp/daos_dfuse/daos_container_copy
total 20971520
drwxr-xr-x 1 hendersp ldap         104 Mar 19 22:37 clients
-rw-r--r-- 1 hendersp ldap 21474836480 Mar 19 22:46 testfile
drwxr-xr-x 1 hendersp ldap         104 Mar 19 22:48 test-dir.0-0
```

!!! note
  For more details on datamover, reference
  [DAOS Support](https://github.com/hpc/mpifileutils/blob/main/DAOS-Support.md)
  on the mpifileutils website.


## Clean Up

1. Remove the copy created using datamover

```sh
$ rm -rf /tmp/daos_dfuse/daos_container_copy
```


2. Remove dfuse mountpoint:

```sh
$ clush -B -w $CLIENT_NODES 'fusermount3 -uz /tmp/daos_dfuse'
$ clush -B -w $CLIENT_NODES rm -rf /tmp/daos_dfuse
```


3. List containers to be destroyed:

```sh
$ daos pool list-containers test_pool
Containers in pool test_pool:
  Label
  -----
  test_cont
  test_cont2
  test_cont3
```


4. Destroy Containers:

```sh
$ daos container destroy test_pool test_cont
Successfully destroyed container test_cont
$ daos container destroy test_pool test_cont2
Successfully destroyed container test_cont2
$ daos container destroy test_pool test_cont3
Successfully destroyed container test_cont3
$

$ daos pool list-containers test_pool
No containers.
```


5. List Pools to be destroyed:

```sh
$ dmg pool list
Pool          Size   State Used Imbalance Disabled
----          ----   ----- ---- --------- --------
autotest_pool 47 GB  Ready 2%   0%        0/24
test_pool     786 GB Ready 0%   0%        0/24
```


6. Destroy Pool:

```sh
$ dmg pool destroy autotest_pool
Pool-destroy command succeeded
$ dmg pool destroy test_pool
Pool-destroy command succeeded
$

$ dmg pool list
No pools in system
```


7. Stop Agents:

```sh
$ clush -B -w $CLIENT_NODES "sudo systemctl stop daos_agent"
```


8. Stop Servers:

```sh
$ clush -B -w $SERVER_NODES "sudo systemctl stop daos_server"
```
