#!/bin/bash

# Hardware Validation script to verify if the single node
# is in deployable state. This is intended to run on a single node.

rc=1

# Verify if all the required rpms are installed
rpms_check(){

prereq_rpms_list=("boost-python36-devel" "clang-analyzer" "cmake" "CUnit-devel" "doxygen" "e2fsprogs" "file" "flex" "fuse3-devel" "gcc" "gcc-c++" "git" "golang" "graphviz" "hwloc-devel" "ipmctl" "java-1.8.0-openjdk" "json-c-devel" "lcov" "libaio-devel" "libcmocka-devel" "libevent-devel" "libipmctl-devel" "libiscsi-devel" "libtool" "libtool-ltdl-devel" "libunwind-devel" "libuuid-devel" "libyaml-devel" "Lmod" "lz4-devel" "make" "man" "maven" "nasm" "ndctl" "numactl" "numactl-devel" "openmpi3-devel" "openssl-devel" "pandoc" "patch" "patchelf" "pciutils" "python36-Cython" "python36-devel" "python36-distro" "python36-junit_xml" "python36-numpy" "python36-paramiko" "python36-pylint" "python36-requests" "python36-tabulate" "python36-pyxattr" "python36-PyYAML" "python36-scons" "sg3_utils" "sudo" "valgrind-devel" "yasm")

daos_rpms_servers=("argobots" "daos" "daos-server" "dpdk" "libfabric" "libpmem" "libpmemobj" "mercury" "protobuf-c" "spdk" "spdk-tools")

daos_rpms_clients=("daos-client" "daos" "libfabric" "mercury" "protobuf-c")

if [[ $1 == "server" ]]; then
    rpms_list=("${prereq_rpms_list[@]}" "${daos_rpms_servers[@]}")
elif [[ $1 == "client" ]]; then
    rpms_list=("${prereq_rpms_list[@]}" "${daos_rpms_clients[@]}")
else
    rpms_list=("${prereq_rpms_list[@]}")
fi

echo "Checking for following list of rpms: ${rpms_list[@]}"

declare -a rpms_installed
declare -a rpms_not_installed

for i in "${!rpms_list[@]}"; do
    rc_rpm=`rpm -qa | grep -q ${rpms_list[i]};echo $?`
    if [ "${rc_rpm}" != 0 ]; then
        rpms_not_installed=(${rpms_not_installed[@]} "${rpms_list[i]}")
    else
        rpms_installed=(${rpms_installed[@]} "${rpms_list[i]}")
    fi
done

echo "Installed rpms: ${rpms_installed[@]}"

if [ ${#rpms_not_installed[@]} != 0 ]; then
    echo "[FAILED] ${rpms_not_installed[@]} not installed"
    exit $rc
fi

}

# Verify with lscpu that VT-x is enabled
vtx_enabled(){

output=`lscpu | grep Virtualization`
awk '$0~/VT-x/{print "VT-x is enabled\n"}' <<< $output

}

# Verify that the number of NUMA nodes is equal to the number of sockets
check_numa_sockets(){

output=`lscpu | egrep -e Socket -e NUMA`
num_sockets=$(echo $output | cut -d ' ' -f 2)
num_numa_nodes=$(echo $output | cut -d ' ' -f 5)
if [ "$num_sockets" -eq "$num_numa_nodes" ]; then
    echo "Number of sockets is equivalent to number of numa nodes"
    echo "Number of sockets: ${num_sockets}"
    echo -e "Number of Numa Nodes: ${num_numa_nodes}\n"
else
    echo "[FAILED] Number of sockets is not equal to number of numa nodes"
    exit $rc
fi

}


# Verify valid IB state
verify_ibstate(){

output=`ibstatus | egrep -e state`
num_active_ports=$(echo $output | awk -F': ACTIVE' 'NF{print NF-1}')
if [ "$num_active_ports" -gt 0 ]; then
    echo -e "Number of active ib ports: ${num_active_ports}\n"
else
    echo "[FAILED] No active ib port"
    exit $rc
fi

}

# Verify all the IB interfaces have IP addresses attached
verify_ip_for_interfaces(){

# get all available interfaces
available_ifaces=`ls /sys/class/net`
echo $available_ifaces
iface_count=$(echo $available_ifaces | awk -F' ' 'NF{print NF-1}')
echo "Total number of interfaces: ${iface_count}"

output=`ip -4 -o a | cut -d ' ' -f 2,7 | cut -d '/' -f 1`
echo $output
set -f
array=(${available_ifaces// / })
declare -a iface_with_ip
declare -a iface_without_ip

for i in "${!array[@]}";do
    if [[ $output =~ "${array[i]}" ]]; then
        echo "IP available for interface: ${array[i]}"
        iface_with_ip=(${iface_with_ip[@]} "${array[i]}")
    else
        echo "IP not available for interface: ${array[i]}"
        iface_without_ip=(${iface_without_ip[@]} "${array[i]}")
    fi
done

pattern='ib'
if [[ "${iface_without_ip[@]}" =~ "${pattern}" ]]; then
    echo "IB interfaces need to IP address set"
    exit $rc
fi
echo -e "\n"

}

# For dual IB setup, verify that the settings for multi-rail are set
check_for_multirail_settings(){

check_for=("net.ipv4.conf.all.accept_local" "net.ipv4.conf.all.arp_ignore" "net.ipv4.conf.ib0.rp_filter" "net.ipv4.conf.ib1.rp_filter")

for i in "${!check_for[@]}"; do
    output=`sysctl "${check_for[i]}"`
    echo $output
    value=$(echo $output | awk -F' ' '{print $3}')
    echo $value
#    if [[ $value != 0 ]]; then    # remove this when running on a hw where multi-rail is set
    if [[ $value == 0 ]]; then
        echo "[FAILED] ${check_for[i]} not set"
        exit $rc
    fi
done

}

# UNCOMMENT WHEN ntpstat IS INSTALLED
# Verify that NTP is configured and clocks are in sync
verify_ntp(){

output=`ntpstat`
if [[ $output =~ 'synchronised to NTP server' ]]; then
    echo "NTP configuration is set"
else
    echo "[FAILED] Check NTP congiration"
    exit $rc
fi

}

# Check SSD health
ssd_health(){

# Are my nvme block devices present
#ls /home/standan/daos.??*
#daos_server storage prepare -p 4096 -u root --reset
daos_server storage prepare --nvme-only -p 4096 -u root --reset
sleep 2
bash -c 'ls -l /dev/nvme*n1'
local rc_ls_nvme=`echo $?`
if [[ $rc_ls_nvme != 0 ]]; then
    echo "[FAILED] No nvme block device found"
    exit $rc
fi

# Identify the Ids of Nvme devices
declare -a nvme_ids
output=`lspci | grep Non-Volatile | awk '{print $1}'`
local nvme_ids=(${output// / })
for i in "${!nvme_ids[@]}"; do
    echo "${nvme_ids[i]}"
done

# prepare nvme ssds
daos_server storage prepare --nvme-only -p 4096 -u root
sleep 5

# run spdk_nvme_perf
for i in "${!nvme_ids[@]}"; do
    local transport_ids+=" -r 'trtype:PCIe traddr:0000:${nvme_ids[i]}'"
done
echo "${transport_ids}"
lsblk
#daos_server storage scan
#run_cmd="spdk_nvme_perf -q 16 -o 1048576 -w write -t 20 -c 0xf00000f ${transport_ids}"
#run_cmd=$(spdk_nvme_perf -q 16 -o 1048576 -w write -t 20 -c 0xf00000f ${transport_ids})
#echo $run_cmd
#`$run_cmd`
bash -c "spdk_nvme_perf -q 16 -o 1048576 -w write -t 100 -c 0xf00000f ${transport_ids}"
rc_spdk_perf=`echo $?`
if [[ $rc_spdk_perf != 0 ]]; then
    echo "[FAILED] SPDK Perf failed"
    exit $rc
fi

}


#rpms_check $1
vtx_enabled
check_numa_sockets
verify_ibstate
verify_ip_for_interfaces
#check_for_multirail_settings
#verify_ntp
ssd_health
