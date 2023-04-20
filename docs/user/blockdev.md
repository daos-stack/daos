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
$ ./configure --with-daos
$ make -j 16
```

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
```

Open another terminal for the configuration process. The configuration is done via scripts/rpc.py script,
after that it can be dumped into json file that later may be passed to nvmf\_tgt.
The shortest way to create couple of disk backed up by DAOS DFS is to use the following script:

```sh
POOL_UUID="${POOL:-pool_label}"
CONT_UUID="${CONT:-const_label}"
BIND_IP="${TARGET_IP:-172.31.91.61}"

sudo ./scripts/rpc.py nvmf_create_transport -t TCP -u 2097152 -i 2097152

for i in $(seq 1 "$1"); do
	sudo ./scripts/rpc.py bdev_daos_create disk$i ${POOL_UUID} ${CONT_UUID} 1048576 4096 --uuid ${DISK_UUID}
	subsystem=nqn.2016-06.io.spdk$i:cnode$i
	sudo scripts/rpc.py nvmf_create_subsystem $subsystem -a -s SPDK0000000000000$i -d SPDK_Virtual_Controller_$i
	sudo scripts/rpc.py nvmf_subsystem_add_ns $subsystem  disk$i
	sudo scripts/rpc.py nvmf_subsystem_add_listener $subsystem -t tcp -a ${BIND_IP} -s 4420
done
```

Subsystem name (NQN) and UUID of the disk are pretty important for multi-pathing and have to match across different controllers (nodes)
The default values are for the dev setup on the daos2 node.

After the successful run of the script:

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
/dev/nvme0n1     PHAC110301JT3P8AGN   INTEL SSDPF2KX038TZ                      1           3.84  TB /   3.84  TB    512   B +  0 B   JCV10100
/dev/nvme1n1     BTAC127500KS3P8AGN   INTEL SSDPF2KX038TZ                      1           3.84  TB /   3.84  TB    512   B +  0 B   JCV10100
/dev/nvme2n1     PHAC1105008P3P8AGN   INTEL SSDPF2KX038TZ                      1           3.84  TB /   3.84  TB    512   B +  0 B   JCV10100
/dev/nvme3n1     PHAC115100DN3P8AGN   INTEL SSDPF2KX038TZ                      1           3.84  TB /   3.84  TB    512   B +  0 B   JCV10100
/dev/nvme4n1     PHAC125100J43P8AGN   INTEL SSDPF2KX038TZ                      1           3.84  TB /   3.84  TB    512   B +  0 B   JCV10100
/dev/nvme5n1     PHAC1105006A3P8AGN   INTEL SSDPF2KX038TZ                      1           3.84  TB /   3.84  TB    512   B +  0 B   JCV10100
/dev/nvme6n1     PHAC110301H23P8AGN   INTEL SSDPF2KX038TZ                      1           3.84  TB /   3.84  TB    512   B +  0 B   JCV10100
/dev/nvme7n1     PHAC122200013P8AGN   INTEL SSDPF2KX038TZ                      1           3.84  TB /   3.84  TB    512   B +  0 B   JCV10100
/dev/nvme8n1     SPDK00000000000001   SPDK_Virtual_Controller_1                1           1.10  TB /   1.10  TB      4 KiB +  0 B   22.05   
/dev/nvme9n1     SPDK00000000000002   SPDK_Virtual_Controller_2                1           1.10  TB /   1.10  TB      4 KiB +  0 B   22.05
```

The block devices can now be accessed to run fio or to mount a filesystem:

```sh
$ sudo mkfs.ext4 /dev/nvme8n1
$ sudo mkdir /testfs
$ sudo mount /dev/nvme8n1 /testfs
```

The resulted filesystem should not be concurrently accessed from different compute nodes. It is recommended to use the ext4 feature called multiple mount protection to avoid this.
It is possible to mount one one node such a filesystem, inject data, unmount it and then mount this filesystem on multiple filesystem in read-only mode.

Once all is done, please clean up after yourself by running on the initiator side:

```sh
$ sudo nvme disconnect-all
```

And shut down the nvmf\_tgt. Otherwise linux kernel might get very upset about missing drives.
