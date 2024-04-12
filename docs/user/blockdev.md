# Block Device

A POSIX container can exported a block device via the NVMe-oF protocol. This requires to set up
a separate SPDK service on third-party nodes (e.g. dedicated nodes or running on specific
cores on client nodes or the DAOS storage nodes) exporting DAOS containers as a NVMe targets.
This section describes how to configure the SPDK DAOS bdev and access it via the NVMe-oF
protocol. It assumes that a DAOS system is already configured.

## Configuring NVMe-oF Target

It is advisable to configure host according with the SPDK performance reports.

Clone the spdk repo and switch to daos branch:

```sh
$ git clone https://github.com/spdk/spdk.git
$ git submodule update --init
$ ./configure --with-daos
$ make -j 16
```

!!! tip
    If DAOS was built from source, use --with-daos=--with-daos=/path/to/daos/install/dir

The output binaries are located under build/bin

!!! note
    Prior to DAOS v2.2 the single thread performance is capped at ~250k 4k IOPS, it should
    be rectified with the patch on the daos branch. Meanwhile, usage of process per disk is preferable.

Hugepages should be then be configured on the host:

```sh
$ sudo HUGE_EVEN_ALLOC=yes scripts/setup.sh
```

!!! note
    HUGE_EVEN_ALLOC=yes is needed to enable hugepages on all NUMA nodes in the system.

In the first terminal run the nvmf target app:

```sh
$ sudo ./build/bin/nvmf_tgt -m [21,22,23,24]
[2023-04-21 09:09:40.791150] Starting SPDK v23.05-pre git sha1 26b9be752 / DPDK 22.11.1 initialization...
[2023-04-21 09:09:40.791194] [ DPDK EAL parameters: nvmf --no-shconf -l 21,22,23,24 --huge-unlink --log-level=lib.eal:6 --log-level=lib.cryptodev:5 --log-level=user1:6 --base-virtaddr=0x200000000000 --match-allocations --file-prefix=spdk_pid747434 ]
TELEMETRY: No legacy callbacks, legacy socket not created
[2023-04-21 09:09:40.830768] app.c: 738:spdk_app_start: *NOTICE*: Total cores available: 4
[2023-04-21 09:09:40.859580] reactor.c: 937:reactor_run: *NOTICE*: Reactor started on core 22
[2023-04-21 09:09:40.859716] reactor.c: 937:reactor_run: *NOTICE*: Reactor started on core 23
[2023-04-21 09:09:40.859843] reactor.c: 937:reactor_run: *NOTICE*: Reactor started on core 24
[2023-04-21 09:09:40.859844] reactor.c: 937:reactor_run: *NOTICE*: Reactor started on core 21
[2023-04-21 09:09:40.878692] accel_sw.c: 601:sw_accel_module_init: *NOTICE*: Accel framework software module initialized.
```

Open another terminal for the configuration process. The configuration is done via scripts/rpc.py script,
after that it can be dumped into json file that later may be passed to nvmf\_tgt.
The shortest way to create couple of disk backed up by DAOS DFS is to use the following script (called export\_disk.sh):

```sh
POOL_UUID="${POOL:-pool_label}"
CONT_UUID="${CONT:-const_label}"
NR_DISKS="${1:-1}"
BIND_IP="${TARGET_IP:-172.31.91.61}"

sudo ./scripts/rpc.py nvmf_create_transport -t TCP -u 2097152 -i 2097152

for i in $(seq 1 "$NR_DISKS"); do
	DISK_UUID="${UUID:-`uuidgen`}"
	sudo ./scripts/rpc.py bdev_daos_create disk$i ${POOL_UUID} ${CONT_UUID} 1048576 4096 --uuid ${DISK_UUID}
	subsystem=nqn.2016-06.io.spdk$i:cnode$i
	sudo scripts/rpc.py nvmf_create_subsystem $subsystem -a -s SPDK0000000000000$i -d SPDK_Virtual_Controller_$i
	sudo scripts/rpc.py nvmf_subsystem_add_ns $subsystem  disk$i
	sudo scripts/rpc.py nvmf_subsystem_add_listener $subsystem -t tcp -a ${BIND_IP} -s 4420
done
```

Subsystem name (NQN) and UUID of the disk are pretty important for multi-pathing and have to match across different controllers (nodes)
The default values are for the dev setup on the daos2 node.

The script optionally takes the number of disk to export:

```sh
denis@daos2:~/spdk> POOL=denisb CONT=nvmetest sh export_disk.sh 2
disk1
disk2
```

## Accessing the Device

On the node where you want to access the block device, make sure that nvme-cli is installed and nvme-tcp module is loaded via: sudo modprobe nvme-tcp.
To connect to the target disk run:

```sh
$ sudo nvme connect-all -t tcp -a 172.31.91.61 -s 4420
```

After the successful execution the new nvme drives should appear in the system:

```sh
$ sudo nvme list
Node             SN                   Model                                    Namespace Usage                      Format           FW Rev
---------------- -------------------- ---------------------------------------- --------- -------------------------- ---------------- --------
/dev/nvme0n1     S4YPNE0N800124       SAMSUNG MZWLJ3T8HBLS-00007               1           3.84  TB /   3.84  TB    512   B +  0 B   EPK98B5Q
/dev/nvme1n1     SPDK00000000000001   SPDK_Virtual_Controller_1                1           1.10  TB /   1.10  TB      4 KiB +  0 B   23.05
/dev/nvme2n1     SPDK00000000000002   SPDK_Virtual_Controller_2                1           1.10  TB /   1.10  TB      4 KiB +  0 B   23.05
```

The block devices can now be accessed to run fio or to mount a filesystem:

```sh
$ sudo mkfs.ext4 /dev/nvme1n1
$ sudo mkdir /testfs
$ sudo mount /dev/nvme1n1 /testfs
$ df -h /testfs
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme1n1   1007G  1.1G  955G   1% /testfs
```

The resulted filesystem should not be concurrently modified from different client nodes. It is recommended to use the ext4 feature called
multiple mount protection to avoid corrupting the ext4 filesystem from different client nodes. This feature can be enabled as follows:

```sh
$ sudo umount /testfs
$ sudo mkfs.ext4 -F -O mmp /dev/nvme1n1
$ sudo e2mmpstatus /dev/nvme1n1
e2mmpstatus: it is safe to mount '/dev/nvme1n1', MMP is clean
$ sudo mount /dev/nvme1n1 /testfs
```

It is possible to mount one one node, inject data, unmount it and then mount this filesystem on multiple nodes in read-only mode (-o ro mount option).

Once all is done, please clean up after yourself by running on the initiator side:

```sh
$ sudo nvme disconnect-all
```

And shut down the nvmf\_tgt. Otherwise linux kernel might get very upset about missing drives.
