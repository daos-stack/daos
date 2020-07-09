#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

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
import uuid
import re
import time
import os

from general_utils import get_file_path, run_task
from command_utils_base import CommandFailure
from ior_test_base import IorTestBase
from write_host_file import write_host_file
from test_utils_pool import TestPool
from ior_utils import IorCommand
from job_manager_utils import Mpirun
from mpio_utils import MpioUtils

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
        self.container_info = {}
        self.set_faulty_device = False

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()
        # Start the servers and agents
        super(ServerFillUp, self).setUp()
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        #Get this IOR Parameter from yaml file
        self.ior_flags = self.params.get("ior_flags",
                                         '/run/ior/iorflags/*')
        self.ior_read_flags = self.params.get("ior_flags",
                                              '/run/ior/iorflags/*', '-r -R')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_transfer_size = str(self.params.get("transfer_size",
                                                     '/run/ior/iorflags/*'))
        self.ior_daos_oclass = self.params.get("obj_class",
                                               '/run/ior/iorflags/*', "SX")
        self.processes = self.params.get("slots", "/run/ior/clientslots/*")
        #Get the number of daos_io_servers
        self.daos_io_servers = (self.server_managers[0].manager
                                .job.yaml.server_params)
        self.out_queue = queue.Queue()

    def get_max_capacity(self, drive_info):
        """Get NVMe storage capacity based on NVMe disk from server yaml file.

        Args:
            drive_info(list): List of disks from each daos_io_servers

        Returns:
            int: Maximum NVMe storage capacity (in GB).

        """
        # Get the Maximum storage space among all the servers.
        drive_capa = []
        for server in self.hostlist_servers:
            for daos_io_server in range(len(self.daos_io_servers)):
                drive_capa.append(sum(drive_info[server]
                                      [daos_io_server].values()))
        print('Maximum Storage space from the servers is {}GB'
              .format((min(drive_capa)) * 0.99))
        #Return the 99% of storage space as it wont be used 100% for
        #pool creation.
        return min(drive_capa) * 0.99

    def get_server_capacity(self):
        """Get Server Pool NVMe storage capacity.

        Args:
            None

        Returns:
            int: Maximum NVMe storage capacity for pool creation (in GB).

        Note: Read the drive sizes from the server using SPDK application.
        This need to be replaced with dmg command when it's available.
        This is time consuming and not a final solution to get the maximum
        capacity of servers
        """
        drive_info = {}
        _spdk_hello_world = "build/external/dev/spdk/examples/nvme/hello_world"
        identify_bin = get_file_path("hello_world", _spdk_hello_world)[0]
        task = run_task(self.hostlist_servers, "{}".format(identify_bin))

        for _rc_code, _node in task.iter_retcodes():
            if _rc_code == 1:
                print("Failed to identified drives on {}".format(_node))
                raise ValueError

        #Get the drive size from each daos_io_servers
        try:
            for buf, nodes in task.iter_buffers():
                output = str(buf).split('\n')
                _tmp_data = {}
                for daos_io_server in range(len(self.daos_io_servers)):
                    drive_size_list = {}
                    for disk in (self.server_managers[0].manager.job.yaml.
                                 server_params[daos_io_server].bdev_list.value):
                        disk_index = output.index('Attached to {}'
                                                  .format(disk)) + 2
                        drive_size_list['{}'.format(disk)] = int(
                            output[disk_index].split('size:')[1][:-2])
                    _tmp_data[daos_io_server] = drive_size_list
                drive_info[nodes[0]] = _tmp_data
        except:
            print('Failed to get the drive information')
            raise

        return self.get_max_capacity(drive_info)

    def start_ior_thread(self, results, operation='Write'):
        """Start IOR write/read threads and wait until all threads are finished.

        Args:
            results (queue): queue for returning thread results
            operation (str): IOR operation for read/write on same container.

        Returns:
            None
        """
        mpio_util = MpioUtils()
        if mpio_util.mpich_installed(self.hostlist_clients) is False:
            self.fail("Exiting Test: Mpich not installed")

        # Define the arguments for the ior_runner_thread method
        ior_cmd = IorCommand()
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(self.server_group, self.pool)
        ior_cmd.daos_oclass.update(self.ior_daos_oclass)
        ior_cmd.api.update(self.ior_apis)
        ior_cmd.transfer_size.update(self.ior_transfer_size)

        #If IOR write calculate the block size based on server % to fill up
        if 'Write' in operation:
            block_size = self.calculate_ior_block_size()
            ior_cmd.block_size.update('{}'.format(block_size))
            ior_cmd.flags.update(self.ior_flags)
            #Store the container UUID and block size used for
            #reading and verification purpose in future.
            self.container_info["{}{}{}"
                                .format(self.ior_daos_oclass,
                                        self.ior_apis,
                                        self.ior_transfer_size)] = [str(
                                            uuid.uuid4()), block_size]
        elif 'Read' in operation:
            ior_cmd.flags.update(self.ior_read_flags)
            #Retrieve the container UUID and block size for reading purpose
            ior_cmd.block_size.update('{}'
                                      .format(self.container_info
                                              ["{}{}{}".format(
                                                  self.ior_daos_oclass,
                                                  self.ior_apis,
                                                  self.ior_transfer_size)][1]))

        # Define the job manager for the IOR command
        manager = Mpirun(ior_cmd, mpitype="mpich")
        manager.job.daos_cont.update(self.container_info
                                     ["{}{}{}"
                                      .format(self.ior_daos_oclass,
                                              self.ior_apis,
                                              self.ior_transfer_size)][0])
        env = ior_cmd.get_default_env(str(manager))
        manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        manager.assign_processes(self.processes)
        manager.assign_environment(env, True)

        # run IOR Command
        try:
            result = manager.run()
        except CommandFailure as _error:
            results.put("FAIL")
        # Exception does not work so better to verify the proper completion too.
        if 'Finished' not in result.stdout:
            results.put("FAIL")

    def calculate_ior_block_size(self):
        """
        Calculate IOR Block size to fill up the Server

        Args:
            None

        Returns:
            block_size(int): IOR Block size
        """
        #Check the replica for IOR object to calculate the correct block size.
        _replica = re.findall(r'_(.+?)G', self.ior_daos_oclass)
        if not _replica:
            replica_server = 1
        #This is for EC Parity
        elif 'P' in _replica[0]:
            replica_server = re.findall(r'\d+', _replica[0])[0]
        else:
            replica_server = _replica[0]

        print('Replica Server = {}'.format(replica_server))
        nvme_free_space = self.pool.get_pool_daos_space()["s_free"][1]
        _tmp_block_size = (((nvme_free_space/100)*self.capacity)/self.processes)
        _tmp_block_size = int(_tmp_block_size / int(replica_server))
        block_size = ((_tmp_block_size/int(self.ior_transfer_size))
                      *int(self.ior_transfer_size))
        return block_size

    def set_device_faulty(self):
        """
        Set the devices to Faulty one by one and wait for rebuild to complete.

        Returns:
            None
        """
        #Get the device ids from all servers and try to eject the disks
        device_ids = get_device_ids(self.dmg, self.hostlist_servers)

        #no_of_servers and no_of_drives can be set from test yaml.
        #1 Server, 1 Drive = Remove single drive from single server
        for num in range(0, self.no_of_servers):
            server = self.hostlist_servers[num]
            for disk_id in range(0, self.no_of_drives):
                self.dmg.hostlist = server
                self.dmg.storage_set_faulty(device_ids[server][disk_id])

                # Wait for rebuild to start
                self.pool.wait_for_rebuild(True)

                # Wait for rebuild to complete
                self.pool.wait_for_rebuild(False)

    def start_ior_load(self):
        """
        Method to Fill up the server. It will get the maximum Storage space and
        create the pool.Fill up the server based on % amount given using IOR.

        Returns:
            None
        """
        #Method to get the storage capacity.Replace with dmg options in future
        #when it's available. This is time consuming so store the size in
        #file to avoid rerunning the same logic for future test cases.
        #This will be only run for first test case.

        avocao_tmp_dir = os.environ['AVOCADO_TESTS_COMMON_TMPDIR']
        capacity_file = os.path.join(avocao_tmp_dir, 'storage_capacity')
        if not os.path.exists(capacity_file):
            #Stop server but do not reset.
            self.stop_servers_noreset()
            total_nvme_capacity = self.get_server_capacity()
            with open(capacity_file,
                      'w') as _file: _file.write('{}'
                                                 .format(total_nvme_capacity))
            #Start the server.
            self.start_servers()
        else:
            total_nvme_capacity = float(open(capacity_file)
                                        .readline().rstrip())

        print("Server Stoarge capacity = {}GB".format(total_nvme_capacity))
        # Create a pool
        self.pool = TestPool(self.context, dmg_command=self.get_dmg_command())
        self.pool.get_params(self)
        self.pool.nvme_size.update('{}G'.format(total_nvme_capacity))
        self.pool.create()
        print("Pool Usage Percentage - Before - {}"
              .format(self.pool.pool_percentage_used()))

        # Create the IOR threads
        job = threading.Thread(target=self.start_ior_thread,
                               kwargs={"results":self.out_queue,
                                       "operation": 'Write'})
        # Launch the IOR thread
        job.start()

        #Set NVMe device faulty if it's set
        if self.set_faulty_device:
            time.sleep(60)
            #Set the device faulty
            self.set_device_faulty()

        # Wait to finish the thread
        job.join()

        # Verify the queue and make sure no FAIL for any IOR run
        while not self.out_queue.empty():
            if self.out_queue.get() == "FAIL":
                self.fail("FAIL")

        print("pool_percentage_used -- After -- {}"
              .format(self.pool.pool_percentage_used()))

        #Check nvme-health command works
        try:
            self.dmg.hostlist = self.hostlist_servers
            self.dmg.storage_query_nvme_health()
        except CommandFailure as _error:
            self.fail("dmg nvme-health failed")
