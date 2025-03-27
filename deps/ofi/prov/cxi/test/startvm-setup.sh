#!/bin/sh
#
# Initialize a VM for CXI testing and run a command.

DBS_DIR=$(realpath "../../../..")

if [[ -z $RUNCMD ]]; then
    RUNCMD="$@"
fi

export LC_ALL=en_US.UTF-8

ulimit -s unlimited
ulimit -l unlimited

modprobe ptp
modprobe iommu_v2 || modprobe amd_iommu_v2
insmod $DBS_DIR/slingshot_base_link/cxi-sbl.ko
insmod $DBS_DIR/sl-driver/knl/cxi-sl.ko
insmod $DBS_DIR/cxi-driver/cxi/cxi-core.ko disable_default_svc=0
insmod $DBS_DIR/cxi-driver/cxi/cxi-user.ko
insmod $DBS_DIR/cxi-driver/cxi/cxi-eth.ko
insmod $DBS_DIR/kdreg2/kdreg2.ko

# Sleep to wait for Ethernet interface to come up
sleep 3

# Locate the first down Ethernet interface and configure it.
regex="eth([0-9]{1}).+DOWN"
eth_id=-1
interfaces="$(ip addr)"
if [[ $interfaces =~ $regex ]]; then
        eth_id=${BASH_REMATCH[1]}
fi

if [ $eth_id -eq -1 ]; then
        echo "Failed to find Ethernet interface"
        exit 1
fi

AMA=`cat /sys/class/net/eth$eth_id/address | awk -F':' '{print "02:00:" $3 ":" $4 ":" $5 ":" $6}'`

ip link set eth$eth_id addr $AMA
ip link set dev eth$eth_id up

# Add pycxi utilities to path
export PATH=$DBS_DIR/pycxi/utils:$PATH

# Initialize pycxi environment
. $DBS_DIR/pycxi/.venv/bin/activate

if [[ ! -z $RUNCMD ]]; then
    $RUNCMD
fi
