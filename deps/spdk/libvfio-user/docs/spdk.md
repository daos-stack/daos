SPDK and libvfio-user
=====================

[SPDK v21.01](https://github.com/spdk/spdk/releases/tag/v21.01) added
experimental support for a virtual NVMe controller called nvmf/vfio-user. The
controller can be used with the same QEMU command line as the one used for
GPIO.

Build QEMU
----------

Use Oracle's QEMU d377d483f9 from https://github.com/oracle/qemu:

	git clone https://github.com/oracle/qemu qemu-orcl
	cd qemu-orcl
	git submodule update --init --recursive
	./configure --enable-multiprocess
	make

Build SPDK
----------

Use SPDK 72a5fa139:

	git clone https://github.com/spdk/spdk
	cd spdk
	git submodule update --init --recursive
	./configure --with-vfio-user
	make


Start SPDK:

	LD_LIBRARY_PATH=build/lib:dpdk/build/lib build/bin/nvmf_tgt &

Create an NVMe controller with a 512MB RAM-based namespace:

	rm -f /var/run/{cntrl,bar0}
	scripts/rpc.py nvmf_create_transport -t VFIOUSER && \
		scripts/rpc.py bdev_malloc_create 512 512 -b Malloc0 && \
		scripts/rpc.py nvmf_create_subsystem nqn.2019-07.io.spdk:cnode0 -a -s SPDK0 && \
		scripts/rpc.py nvmf_subsystem_add_ns nqn.2019-07.io.spdk:cnode0 Malloc0 && \
		scripts/rpc.py nvmf_subsystem_add_listener nqn.2019-07.io.spdk:cnode0 -t VFIOUSER -a /var/run -s 0

Start the Guest
---------------

Start the guest with e.g. 4 GB of RAM:

	qemu-orcl/build/qemu-system-x86_64 ... \
		-m 4G -object memory-backend-file,id=mem0,size=4G,mem-path=/dev/hugepages,share=on,prealloc=yes -numa node,memdev=mem0 \
		-device vfio-user-pci,socket=/var/run/cntrl


Live Migration
--------------

[SPDK v22.01](https://github.com/spdk/spdk/releases/tag/v22.01) has
[experimental support for live migration](https://spdk.io/release/2022/01/27/22.01_release/).
[This CR](https://review.spdk.io/gerrit/c/spdk/spdk/+/11745/11) contains
additional fixes that make live migration more reliable. Check it out and build
SPDK as explained in [Build SPDK](), both on the source and on the destination
hosts.

Then build QEMU as explained in [Build QEMU]() using the following version:

    https://github.com/oracle/qemu/tree/vfio-user-dbfix

Start the guest at the source host as explained in
[Start the Guest](), appending the `x-enable-migration=on` argument to the
`vfio-user-pci` option.

Then, at the destination host, start the nvmf/vfio-user target and QEMU,
passing the `-incoming` option to QEMU:

    -incoming tcp:0:4444

QEMU will block at the destination waiting for the guest to be migrated.

Bear in mind that if the guest's disk don't reside in shared storage you'll get
I/O errors soon after migration. The easiest way around this is to put the
guest's disk on some NFS mount and share between the source and destination
hosts.

Finally, migrate the guest by issuing the `migrate` command on the QEMU
monitor (enter CTRL-A + C to enter the monitor):

    migrate -d tcp:<destination host IP address>:4444

Migration should happen almost instantaneously, there's no message to show that
migration finished neither in the source nor on the destination hosts. Simply
hitting ENTER at the destination is enough to tell that migration finished.

Finally, type `q` in the source QEMU monitor to exit source QEMU.

For more information in live migration see
https://www.linux-kvm.org/page/Migration.

libvirt
-------

To use the nvmf/vfio-user target with a libvirt quest, in addition to the
libvirtd configuration documented in the [README](../README.md) the guest RAM must
be backed by hugepages:

    <memoryBacking>
      <hugepages>
        <page size='2048' unit='KiB'/>
      </hugepages>
      <source type='memfd'/>
      <access mode='shared'/>
    </memoryBacking>

Because SPDK must be run as root, either fix the vfio-user socket permissions
or configure libvirt to run QEMU as root.
