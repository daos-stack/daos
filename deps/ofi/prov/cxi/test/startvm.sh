#!/bin/bash

# Start a VM, optionally load the test driver, and exit.

# The parameters are given to netsim. See netsim -h.
#   ./startvm.sh       -> run one instance with 1 NIC
#   ./startvm.sh -N 3  -> run one instance with 3 NICs
#   ./startvm.sh -n 2  -> launch 2 VMs each with 1 NIC
#
#
# Note: When using multiple VMs, it is recommended to set the USE_XTERM
# variable. Each VM will be opened in a new xterm window.
#
# USE_XTERM=1 ./startvm.sh -n 2

cd `dirname $0`

DBS_DIR=$(pwd)/../../../..
VIRTME_DIR=$DBS_DIR/virtme
QEMU_DIR=$DBS_DIR/cassini-qemu/x86_64-softmmu/

# If the emulator is not running, start it. This script must run under
# its control, so qemu can connect to it. Simply relaunch self under
# netsim's control.
if [[ ! -v NETSIM_ID ]]; then
	exec $DBS_DIR/nic-emu/netsim $@ $(basename $0)
fi

# Check whether this script is already in a VM or not (ie. running
# under a hypervisor.) If not, we'll need a different setup for nested
# VMs.
HYP=$(grep -c "^flags.*\ hypervisor" /proc/cpuinfo)

if [[ $NETSIM_NICS -eq 1 ]]; then
	CCN_OPTS="-device ccn,addr=8"
elif [[ $NETSIM_NICS -eq 2 ]]; then
	CCN_OPTS="-device ccn,addr=8 -device ccn,addr=13"
elif [[ $NETSIM_NICS -eq 4 ]]; then
	CCN_OPTS="-device ccn,addr=8 -device ccn,addr=0xd -device ccn,addr=0x12 -device ccn,addr=0x17"
fi

# -M q35 = Standard PC (Q35 + ICH9, 2009) (alias of pc-q35-2.10)
# MSI-X needs interrupt remapping enabled to fully work.
# w/ Intel IOMMU. Intremap on requires kernel-irqchip=off OR kernel-irqchip=split
QEMU_OPTS="--qemu-opts -machine q35,kernel-irqchip=split -machine q35 -global q35-pcihost.pci-hole64-size=64G -device intel-iommu,intremap=on,caching-mode=on -smp 4 $CCN_OPTS"
KERN_OPTS="--kopt iommu=pt --kopt intel_iommu=on --kopt iomem=relaxed"
KERN_OPTS="$KERN_OPTS --kopt transparent_hugepage=never --kopt hugepagesz=1g --kopt default_hugepagesz=1g --kopt hugepages=1 --kopt pci=realloc"
KERN_OPTS="$KERN_OPTS --kopt hugepagesz=2M --kopt hugepages=256"

if [[ $HYP -eq 0 ]]; then
	# First VM needs more memory to launch nested VMs
	# Only the first VM will have the CCN qemu device. Nested VMs will
	# have VFs exported to them
	QEMU_OPTS="$QEMU_OPTS -m 8192"

	if [[ -n $QEMU_MOPTS ]]; then
		QEMU_OPTS="$QEMU_OPTS $QEMU_MOPTS"
	fi
else
	# Nested VM. Use the first PCI VF
	# PCIFN = 0000:00:14.0 or similar

	# Bind cxi1 to get its info
	echo 1 > /sys/class/cxi/cxi0/device/sriov_numvfs
	PCIFN=$(basename $(readlink /sys/class/cxi/cxi0/device/virtfn0))
	VENDOR=$(cat /sys/class/cxi/cxi0/device/virtfn0/vendor)
	DEVICE=$(cat /sys/class/cxi/cxi0/device/virtfn0/device)

	# Unbind VF from cxi core driver. cxi1 no longer exists
	echo $PCIFN > /sys/bus/pci/drivers/cxi_core/unbind

	# Bind the VF to vfio driver
	modprobe vfio_pci
	echo ${VENDOR##*x} ${DEVICE##*x} > /sys/bus/pci/drivers/vfio-pci/new_id

	# Tell qemu to bind the VF
	QEMU_OPTS="$QEMU_OPTS -device vfio-pci,host=$PCIFN"
fi

PATH=$QEMU_DIR:$VIRTME_DIR:/sbin:$PATH

VIRTME_OPTS="--rwdir=$(pwd) --pwd"

if [[ $KDIR ]]; then
	VIRTME_OPTS="--kdir $KDIR --mods=auto $VIRTME_OPTS"
else
	VIRTME_OPTS="--installed-kernel $VIRTME_OPTS"
fi

if [[ $MOS ]]; then
	QEMU_OPTS="$QEMU_OPTS -m 2048"
	KERN_OPTS="$KERN_OPTS --kopt kernelcore=1024M --kopt lwkcpus=0.1-3 --kopt lwkmem=1G"
fi

SETUP_SCRIPT="`dirname $0`/startvm-setup.sh"

# Start the VM, execute the script inside, and exit ...
if [[ $RUNCMD ]]; then
    virtme-run --script-sh "$SETUP_SCRIPT $RUNCMD" $VIRTME_OPTS $KERN_OPTS $QEMU_OPTS

# ... or start a VM and execute the script but don't exit
elif [[ $USE_XTERM -eq 1 ]]; then
	xterm -e "virtme-run --init-sh '$SETUP_SCRIPT' $VIRTME_OPTS $KERN_OPTS $QEMU_OPTS"
else
	virtme-run --init-sh "$SETUP_SCRIPT" $VIRTME_OPTS $KERN_OPTS $QEMU_OPTS
fi

# ... or just start a clean VM
#virtme-run --installed-kernel --pwd $KERN_OPTS $QEMU_OPTS
