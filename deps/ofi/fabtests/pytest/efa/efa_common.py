import subprocess
import functools
from common import SshConnectionError, is_ssh_connection_error, has_ssh_connection_err_msg, ClientServerTest
from retrying import retry

def efa_run_client_server_test(cmdline_args, executable, iteration_type,
                               completion_semantic, memory_type, message_size,
                               warmup_iteration_type=None, timeout=None,
                               completion_type="queue"):
    if timeout is None:
        timeout = cmdline_args.timeout

    # It is observed that cuda tests requires larger time-out limit to test all
    # message sizes (especailly when running with multiple workers).
    if "cuda" in memory_type:
        timeout = max(1000, timeout)

    test = ClientServerTest(cmdline_args, executable, iteration_type,
                            completion_semantic=completion_semantic,
                            datacheck_type="with_datacheck",
                            message_size=message_size,
                            memory_type=memory_type,
                            timeout=timeout,
                            warmup_iteration_type=warmup_iteration_type,
                            completion_type=completion_type)
    test.run()

@retry(retry_on_exception=is_ssh_connection_error, stop_max_attempt_number=3, wait_fixed=5000)
def efa_retrieve_hw_counter_value(hostname, hw_counter_name, efa_device_name=None):
    """
    retrieve the value of EFA's hardware counter
    hostname: a host that has efa
    hw_counter_name: EFA hardware counter name. Options are: lifespan, rdma_read_resp_bytes, rdma_read_wrs,recv_wrs,
                     rx_drops, send_bytes, tx_bytes, rdma_read_bytes,  rdma_read_wr_err, recv_bytes, rx_bytes, rx_pkts, send_wrs, tx_pkts
    efa_device_name: Name of the EFA device. Corresponds to the name of the EFA device's directory
    return: an integer that is sum of all EFA device's counter
    """

    if efa_device_name:
        efa_device_dir = efa_device_name
    else:
        efa_device_dir = '*'

    command = 'ssh {} cat "/sys/class/infiniband/{}/ports/*/hw_counters/{}"'.format(hostname, efa_device_dir, hw_counter_name)
    process = subprocess.run(command, shell=True, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8")
    if process.returncode != 0:
        if process.stderr and has_ssh_connection_err_msg(process.stderr):
            print("encountered ssh connection issue")
            raise SshConnectionError()
        # this can happen when OS is using older version of EFA kernel module
        return None

    linelist = process.stdout.split()
    sumvalue = 0
    for strvalue in linelist:
        sumvalue += int(strvalue)
    return sumvalue

def has_gdrcopy(hostname):
    """
    determine whether a host has gdrcopy installed
    hostname: a host
    return: a boolean
    """
    command = "ssh {} /bin/bash --login -c lsmod | grep gdrdrv".format(hostname)
    process = subprocess.run(command, shell=True, check=False, stdout=subprocess.PIPE)
    return process.returncode == 0

def efa_retrieve_gid(hostname):
    """
    return the GID of efa device on a host
    hostname: a host
    return: a string if the host has efa device,
            None otherwise
    """
    command = "ssh {} ibv_devinfo  -v | grep GID | awk '{{print $NF}}' | head -n 1".format(hostname)
    try:
        process = subprocess.run(command, shell=True, check=True, stdout=subprocess.PIPE)
    except subprocess.CalledProcessError:
        # this can happen on instance without EFA device
        return None

    return process.stdout.decode("utf-8").strip()

@retry(retry_on_exception=is_ssh_connection_error, stop_max_attempt_number=3, wait_fixed=5000)
def get_efa_domain_names(server_id):
    timeout = 60
    process_timed_out = False

    # This command returns a list of EFA domain names and its related info
    command = "ssh {} fi_info -p efa".format(server_id)
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8")
 
    try:
        p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        p.terminate()
        process_timed_out = True

    assert not process_timed_out, "Process timed out"
    
    errors = p.stderr.readlines()
    for error in errors:
        error = error.strip()
        if "fi_getinfo: -61" in error:
            raise Exception("No EFA devices/domain names found")

        if has_ssh_connection_err_msg(error):
            raise SshConnectionError()

    efa_domain_names = []
    for line in p.stdout:
        line = line.strip()
        if 'domain' in line:
            domain_name = line.split(': ')[1]
            efa_domain_names.append(domain_name)

    return efa_domain_names

@functools.lru_cache(10)
@retry(retry_on_exception=is_ssh_connection_error, stop_max_attempt_number=3, wait_fixed=5000)
def get_efa_device_names(server_id):
    timeout = 60
    process_timed_out = False

    # This command returns a list of EFA devices names
    command = "ssh {} ibv_devices".format(server_id)
    proc = subprocess.run(command, shell=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          encoding="utf-8", timeout=timeout)

    if has_ssh_connection_err_msg(proc.stderr):
        raise SshConnectionError()

    devices = []
    stdouts = proc.stdout.strip().split("\n")
    #
    # Example out of ibv_devices are like the following:
    #     device                 node GUID
    #     ------              ----------------
    #     rdmap16s27          0000000000000000
    #     ...
    #
    # The first 2 lines are headers, and is ignored.
    for line in stdouts[2:]:
        devices.append(line.split()[0])
    return devices


def get_efa_device_name_for_cuda_device(ip, cuda_device_id, num_cuda_devices):
    # this function implemented a simple way to find the closest EFA device for a given
    # cuda device. It assumes EFA devices names are in order (which is usually true but not always)
    #
    # For example, one a system with 8 CUDA devies and 4 EFA devices, this function would
    # for GPU 0 and 1, return EFA device 0
    # for GPU 2 and 3, return EFA device 1
    # for GPU 4 and 5, return EFA device 2
    # for GPU 6 and 7, return EFA device 3
    efa_devices = get_efa_device_names(ip)
    num_efa = len(efa_devices)
    return efa_devices[(cuda_device_id * num_efa) // num_cuda_devices]
