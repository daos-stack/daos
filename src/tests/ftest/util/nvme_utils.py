#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
import threading
import re
import time
import os

from general_utils import run_task
from command_utils_base import CommandFailure
from avocado.core.exceptions import TestFail
from ior_test_base import IorTestBase
from test_utils_pool import TestPool
from ior_utils import IorCommand

try:
    # python 3.x
    import queue
except ImportError:
    # python 2.7
    import Queue as queue

def get_device_ids(dmg, servers):
    """Get the NVMe Device ID from servers

    Args:
        dmg: DmgCommand class instance.
        servers (list): list of server hosts.

    Returns:
        devices (dictionary): Device UUID for servers.

    """
    devices = {}
    dmg.set_sub_command("storage")
    dmg.sub_command_class.set_sub_command("query")
    dmg.sub_command_class.sub_command_class.set_sub_command("list-devices")
    for host in servers:
        dmg.hostlist = host
        try:
            result = dmg.run()
        except CommandFailure as _error:
            raise "dmg command failed for list-devices"
        drive_list = []
        for line in result.stdout.split('\n'):
            if 'UUID' in line:
                drive_list.append(line.split(':')[1])
        devices[host] = drive_list
    return devices

class ServerFillUp(IorTestBase):
    """
    Class to fill up the servers based on pool percentage given.
    It will get the drives listed in yaml file and find the maximum capacity of
    the pool which will be created.
    IOR block size will be calculated as part of function based on percentage
    of pool needs to fill up.
    """
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-instance-attributes
    def __init__(self, *args, **kwargs):
        """Initialize a IorTestBase object."""
        super(ServerFillUp, self).__init__(*args, **kwargs)
        self.no_of_pools = 1
        self.capacity = 1
        self.no_of_servers = 1
        self.no_of_drives = 1
        self.pool = None
        self.dmg = None
        self.set_faulty_device = False
        self.scm_fill = False
        self.nvme_fill = False
        self.ior_matrix = None

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()
        # Start the servers and agents
        super(ServerFillUp, self).setUp()
        self.hostfile_clients = None
        self.ior_default_flags = self.ior_cmd.flags.value
        self.ior_scm_xfersize = self.ior_cmd.transfer_size.value
        self.ior_read_flags = self.params.get("read_flags",
                                              '/run/ior/iorflags/*',
                                              '-r -R -k -G 1')
        self.ior_nvme_xfersize = self.params.get(
            "nvme_transfer_size", '/run/ior/transfersize_blocksize/*',
            '16777216')
        #Get the number of daos_io_servers
        self.daos_io_servers = (self.server_managers[0].manager
                                .job.yaml.server_params)
        self.out_queue = queue.Queue()

    def get_max_capacity(self, mem_size_info):
        """Get storage capacity based on server yaml file.

        Args:
            mem_size_info(dict): List of NVMe/SCM size from each servers

        Returns:
            int: Maximum NVMe storage capacity.

        """
        # Get the Maximum storage space among all the servers.
        drive_capa = []
        for server in self.hostlist_servers:
            for daos_io_server in range(len(self.daos_io_servers)):
                drive_capa.append(sum(mem_size_info[server][daos_io_server]))
        print('Maximum Storage space from the servers is {}'
              .format(int(min(drive_capa) * 0.96)))

        #Return the 99% of storage space as it won't be used 100% for
        #pool creation.
        return int(min(drive_capa) * 0.96)

    def get_scm_lsblk(self):
        """Get SCM size using lsblk from servers.

        Returns:
            dict: Dictionary of server mapping with disk ID and size
                  'wolf-A': {'nvme2n1': '1600321314816'}.
        """
        scm_data = {}

        task = run_task(self.hostlist_servers, "lsblk -b | grep pmem")
        for _rc_code, _node in task.iter_retcodes():
            if _rc_code == 1:
                print("Failed to lsblk on {}".format(_node))
                raise ValueError
        #Get the drive size from each daos_io_servers
        for buf, nodelist in task.iter_buffers():
            for node in nodelist:
                pcmem_data = {}
                output = str(buf).split('\n')
                for _tmp in output:
                    pcmem_data[_tmp.split()[0]] = _tmp.split()[3]
                scm_data['{}'.format(node)] = pcmem_data

        return scm_data

    def get_nvme_lsblk(self):
        """Get NVMe size using lsblk from servers.

        Returns:
            dict: Dictionary of server mapping with disk ID and size
                  'wolf-A': {'nvme2n1': '1600321314816'}.
        """
        nvme_data = {}

        task = run_task(self.hostlist_servers, "lsblk -b /dev/nvme*n*")
        for _rc_code, _node in task.iter_retcodes():
            if _rc_code == 1:
                print("Failed to lsblk on {}".format(_node))
                raise ValueError
        #Get the drive size from each daos_io_servers
        for buf, nodelist in task.iter_buffers():
            for node in nodelist:
                disk_data = {}
                output = str(buf).split('\n')
                for _tmp in output[1:]:
                    if 'nvme' in _tmp:
                        disk_data[_tmp.split()[0]] = _tmp.split()[3]
                    nvme_data['{}'.format(node)] = disk_data

        return nvme_data

    def get_nvme_readlink(self):
        """Get NVMe readlink from servers.

        Returns:
            dict: Dictionary of server readlink pci mapping with disk ID
                  'wolf-A': {'0000:da:00.0': 'nvme9n1'}.
                  Dictionary of server mapping with disk ID and size
                  'wolf-A': {'nvme2n1': '1600321314816'}.
        """
        nvme_lsblk = self.get_nvme_lsblk()
        nvme_readlink = {}

        #Create the dictionary for NVMe readlink.
        for server, items in nvme_lsblk.items():
            tmp_dict = {}
            for drive in items:
                cmd = ('readlink /sys/block/{}/device/device'
                       .format(drive.split()[0]))
                task = run_task([server], cmd)
                for _rc_code, _node in task.iter_retcodes():
                    if _rc_code == 1:
                        print("Failed to readlink on {}".format(_node))
                        raise ValueError
                #Get the drive size from each daos_io_servers
                for buf, _node in task.iter_buffers():
                    output = str(buf).split('\n')
                tmp_dict[output[0].split('/')[-1]] = drive.split()[0]
            nvme_readlink[server] = tmp_dict

        return nvme_lsblk, nvme_readlink

    def get_scm_max_capacity(self):
        """Check with server.yaml and return maximum SCM size allow to create.

        Returns:
            int: Maximum NVMe storage capacity for pool creation.

        Note: Read the PCMEM sizes from the server using lsblk command.
        This need to be replaced with dmg command when it's available.
        """
        scm_lsblk = self.get_scm_lsblk()

        scm_size = {}
        #Create the dictionary for Max SCM size for all the servers.
        for server in scm_lsblk:
            tmp_dict = {}
            for daos_io_server in range(len(self.daos_io_servers)):
                tmp_disk_list = []
                for pcmem in (self.server_managers[0].manager.job.yaml.
                              server_params[daos_io_server].scm_list.value):
                    pcmem_num = pcmem.split('/')[-1]
                    if pcmem_num in scm_lsblk[server].keys():
                        tmp_disk_list.append(int(scm_lsblk[server][pcmem_num]))
                    else:
                        self.fail("PCMEM {} can not found on server {}"
                                  .format(pcmem, server))
                tmp_dict[daos_io_server] = tmp_disk_list
            scm_size[server] = tmp_dict

        return self.get_max_capacity(scm_size)

    def get_nvme_max_capacity(self):
        """Get Server NVMe storage maximum capacity.

        Returns:
            int: Maximum NVMe storage capacity for pool creation.

        Note: Read the drive sizes from the server using lsblk command.
        This need to be replaced with dmg command when it's available.
        This is time consuming and not a final solution to get the maximum
        capacity of servers.
        """
        drive_info = {}
        nvme_lsblk, nvme_readlink = self.get_nvme_readlink()

        #Create the dictionary for NVMe size for all the servers and drives.
        for server in nvme_lsblk:
            tmp_dict = {}
            for daos_io_server in range(len(self.daos_io_servers)):
                tmp_disk_list = []
                for disk in (self.server_managers[0].manager.job.yaml.
                             server_params[daos_io_server].bdev_list.value):
                    if disk in nvme_readlink[server].keys():
                        size = int(nvme_lsblk[server]
                                   [nvme_readlink[server][disk]])
                        tmp_disk_list.append(size)
                    else:
                        self.fail("Disk {} can not found on server {}"
                                  .format(disk, server))
                tmp_dict[daos_io_server] = tmp_disk_list
            drive_info[server] = tmp_dict

        return self.get_max_capacity(drive_info)

    def start_ior_thread(self, results, operation='WriteRead'):
        """Start IOR write/read threads and wait until all threads are finished.

        Args:
            results (queue): queue for returning thread results
            operation (str): IOR operation for read/write.
                             Default it will do whatever mention in ior_flags
                             set.
        """
        _create_cont = True
        self.ior_cmd.flags.value = self.ior_default_flags
        #For IOR Read only operation, retrieve the stored container UUID
        if 'Read' in operation:
            _create_cont = False
            self.ior_cmd.flags.value = self.ior_read_flags

        #For IOR Other operation, calculate the block size based on server %
        #to fill up. Store the container UUID for future reading operation.
        block_size = self.calculate_ior_block_size()
        self.ior_cmd.block_size.update('{}'.format(block_size))

        # run IOR Command
        try:
            out = self.run_ior_with_pool(create_cont=_create_cont)
            self.ior_matrix = IorCommand.get_ior_metrics(out)
            results.put("PASS")
        except (CommandFailure, TestFail) as _error:
            results.put("FAIL")

    def calculate_ior_block_size(self):
        """
        Calculate IOR Block size to fill up the Server

        Returns:
            block_size(int): IOR Block size
        """
        #Check the replica for IOR object to calculate the correct block size.
        _replica = re.findall(r'_(.+?)G', self.ior_cmd.dfs_oclass.value)
        if not _replica:
            replica_server = 1
        #This is for EC Parity
        elif 'P' in _replica[0]:
            replica_server = re.findall(r'\d+', _replica[0])[0]
        else:
            replica_server = _replica[0]

        print('Replica Server = {}'.format(replica_server))
        if self.scm_fill:
            free_space = self.pool.get_pool_daos_space()["s_total"][0]
            self.ior_cmd.transfer_size.value = self.ior_scm_xfersize
        elif self.nvme_fill:
            free_space = self.pool.get_pool_daos_space()["s_total"][1]
            self.ior_cmd.transfer_size.value = self.ior_nvme_xfersize
        else:
            self.fail('Provide storage type (SCM/NVMe) to be filled')

        #Get the block size based on the capacity to be filled. For example
        #If nvme_free_space is 100G and to fill 50% of capacity.
        #Formula : (107374182400 / 100) * 50.This will give 50% of space to be
        #filled. Divide with total number of process, 16 process means each
        #process will write 3.12Gb.last, if there is replica set, For RP_2G1
        #will divide the individual process size by number of replica.
        #3.12G (Single process size)/2 (No of Replica) = 1.56G
        #To fill 50 % of 100GB pool with total 16 process and replica 2, IOR
        #single process size will be 1.56GB.
        _tmp_block_size = (((free_space/100)*self.capacity)/self.processes)
        _tmp_block_size = int(_tmp_block_size / int(replica_server))
        block_size = ((_tmp_block_size/int(self.ior_cmd.transfer_size.value))
                      *int(self.ior_cmd.transfer_size.value))
        return block_size

    def set_device_faulty(self, server, disk_id):
        """
        Set the devices (disk_id) to Faulty and wait for rebuild to complete on
        given server hostname.

        args:
            server(string): server hostname where it generate the NVMe fault.
            disk_id(string): NVMe disk ID where it will be changed to faulty.
        """
        self.dmg.hostlist = server
        self.dmg.storage_set_faulty(disk_id)
        result = self.dmg.storage_query_device_health(disk_id)
        #Check if device state changed to FAULTY.
        if 'State:FAULTY' not in result.stdout:
            self.fail("device State {} on host {} suppose to be FAULTY"
                      .format(disk_id, server))
        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True)
        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

    def set_device_faulty_loop(self):
        """
        Set the devices to Faulty one by one and wait for rebuild to complete.
        """
        #Get the device ids from all servers and try to eject the disks
        device_ids = get_device_ids(self.dmg, self.hostlist_servers)

        #no_of_servers and no_of_drives can be set from test yaml.
        #1 Server, 1 Drive = Remove single drive from single server
        for num in range(0, self.no_of_servers):
            server = self.hostlist_servers[num]
            for disk_id in range(0, self.no_of_drives):
                self.set_device_faulty(server, device_ids[server][disk_id])

    def create_pool_max_size(self, scm=False, nvme=False):
        """
        Method to create the single pool with Maximum NVMe/SCM size available.

        arg:
            scm(bool): To create the pool with max SCM size or not.
            nvme(bool): To create the pool with max NVMe size or not.

        Note: Method to Fill up the server. It will get the maximum Storage
              space and create the pool.
              Replace with dmg options in future when it's available.
        """
        # Create a pool
        self.pool = TestPool(self.context, dmg_command=self.get_dmg_command())
        self.pool.get_params(self)

        #If NVMe is True get the max NVMe size from servers
        if nvme:
            avocao_tmp_dir = os.environ['AVOCADO_TESTS_COMMON_TMPDIR']
            capacity_file = os.path.join(avocao_tmp_dir, 'storage_capacity')
            if not os.path.exists(capacity_file):
                #Stop servers.
                self.stop_servers()
                total_nvme_capacity = self.get_nvme_max_capacity()
                with open(capacity_file,
                          'w') as _file: _file.write(
                              '{}'.format(total_nvme_capacity))
                #Start the server.
                self.start_servers()
            else:
                total_nvme_capacity = open(capacity_file).readline().rstrip()

            print("Server NVMe Max Storage capacity = {}"
                  .format(total_nvme_capacity))
            self.pool.nvme_size.update('{}'.format(total_nvme_capacity))

        #If SCM is True get the max SCM size from servers
        if scm:
            total_scm_capacity = self.get_scm_max_capacity()
            print("Server SCM Max Storage capacity = {}"
                  .format(total_scm_capacity))
            self.pool.scm_size.update('{}'.format(total_scm_capacity))

        #Create the Pool
        self.pool.create()

    def start_ior_load(self, storage='NVMe', operation="Write", percent=1):
        """
        Method to Fill up the server either SCM or NVMe.
        Fill up based on percent amount given using IOR.

        arg:
            storage(string): SCM or NVMe, by default it will fill NVMe.
            operation(string): Write/Read operation
            percent(int): % of storage to be filled

        Returns:
            None
        """
        self.capacity = percent
        # Fill up NVMe by default
        self.nvme_fill = True if 'NVMe' in storage else False
        self.scm_fill = True if 'SCM' in storage else False

        if operation not in ['Read', 'Write']:
            self.fail('Please provide the valid IO operation instead {}'
                      .format(operation))

        # Create the IOR threads
        job = threading.Thread(target=self.start_ior_thread,
                               kwargs={"results":self.out_queue,
                                       "operation": operation})
        # Launch the IOR thread
        job.start()

        #Set NVMe device faulty if it's set
        if self.set_faulty_device:
            time.sleep(60)
            #Set the device faulty
            self.set_device_faulty_loop()

        # Wait to finish the thread
        job.join()

        # Verify the queue and make sure no FAIL for any IOR run
        while not self.out_queue.empty():
            if self.out_queue.get() == "FAIL":
                self.fail("FAIL")
