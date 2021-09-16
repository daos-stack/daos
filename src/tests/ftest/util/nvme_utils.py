#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import threading
import re
import time
import queue
from command_utils_base import CommandFailure
from avocado.core.exceptions import TestFail
from ior_test_base import IorTestBase
from ior_utils import IorCommand
from server_utils import ServerFailed


def get_device_ids(dmg, servers):
    """Get the NVMe Device ID from servers.

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
            raise CommandFailure(
                "dmg list-devices failed with error {}".format(
                    _error)) from _error
        drive_list = []
        for line in result.stdout_text.split('\n'):
            if 'UUID' in line:
                drive_list.append(line.split('UUID:')[1].split(' ')[0])
        devices[host] = drive_list
    return devices


class ServerFillUp(IorTestBase):
    # pylint: disable=too-many-ancestors,too-many-instance-attributes
    """Class to fill up the servers based on pool percentage given.

    It will get the drives listed in yaml file and find the maximum capacity of
    the pool which will be created.
    IOR block size will be calculated as part of function based on percentage
    of pool needs to fill up.
    """

    def __init__(self, *args, **kwargs):
        """Initialize a IorTestBase object."""
        super().__init__(*args, **kwargs)
        self.no_of_pools = 1
        self.capacity = 1
        self.no_of_servers = 1
        self.no_of_drives = 1
        self.pool = None
        self.dmg = None
        self.set_faulty_device = False
        self.set_online_rebuild = False
        self.rank_to_kill = None
        self.scm_fill = False
        self.nvme_fill = False
        self.ior_matrix = None
        self.fail_on_warning = False

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()
        # Start the servers and agents
        super().setUp()
        self.hostfile_clients = None
        self.ior_default_flags = self.ior_cmd.flags.value
        self.ior_scm_xfersize = self.ior_cmd.transfer_size.value
        self.ior_read_flags = self.params.get("read_flags",
                                              '/run/ior/iorflags/*',
                                              '-r -R -k -G 1')
        self.ior_nvme_xfersize = self.params.get(
            "nvme_transfer_size", '/run/ior/transfersize_blocksize/*',
            '16777216')
        # Get the number of daos_engine
        self.engines = (self.server_managers[0].manager.job.yaml.engine_params)
        self.out_queue = queue.Queue()

    def start_ior_thread(self, results, create_cont, operation='WriteRead'):
        """Start IOR write/read threads and wait until all threads are finished.

        Args:
            results (queue): queue for returning thread results
            operation (str): IOR operation for read/write.
                             Default it will do whatever mention in ior_flags
                             set.
        """
        self.ior_cmd.flags.value = self.ior_default_flags

        # For IOR Other operation, calculate the block size based on server %
        # to fill up. Store the container UUID for future reading operation.
        if operation == 'Write':
            block_size = self.calculate_ior_block_size()
            self.ior_cmd.block_size.update('{}'.format(block_size))
        # For IOR Read only operation, retrieve the stored container UUID
        elif operation == 'Read':
            create_cont = False
            self.ior_cmd.flags.value = self.ior_read_flags

        # run IOR Command
        try:
            out = self.run_ior_with_pool(create_cont=create_cont,
                                         fail_on_warning=self.fail_on_warning)
            self.ior_matrix = IorCommand.get_ior_metrics(out)
            results.put("PASS")
        except (CommandFailure, TestFail) as _error:
            results.put("FAIL")

    def calculate_ior_block_size(self):
        """Calculate IOR Block size to fill up the Server.

        Returns:
            block_size(int): IOR Block size

        """
        # Check the replica for IOR object to calculate the correct block size.
        _replica = re.findall(r'_(.+?)G', self.ior_cmd.dfs_oclass.value)
        if not _replica:
            replica_server = 1
        # This is for EC Parity
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

        # Get the block size based on the capacity to be filled. For example
        # If nvme_free_space is 100G and to fill 50% of capacity.
        # Formula : (107374182400 / 100) * 50.This will give 50% of space to be
        # filled. Divide with total number of process, 16 process means each
        # process will write 3.12Gb.last, if there is replica set, For RP_2G1
        # will divide the individual process size by number of replica.
        # 3.12G (Single process size)/2 (No of Replica) = 1.56G
        # To fill 50 % of 100GB pool with total 16 process and replica 2, IOR
        # single process size will be 1.56GB.
        _tmp_block_size = (((free_space/100)*self.capacity)/self.processes)
        _tmp_block_size = int(_tmp_block_size / int(replica_server))
        block_size = (
            int(_tmp_block_size / int(self.ior_cmd.transfer_size.value)) *
            int(self.ior_cmd.transfer_size.value))
        return block_size

    def set_device_faulty(self, server, disk_id):
        """Set the devices to Faulty and wait for rebuild to complete.

        Args:
            server (string): server hostname where it generate the NVMe fault.
            disk_id (string): NVMe disk ID where it will be changed to faulty.
        """
        self.dmg.hostlist = server
        self.dmg.storage_set_faulty(disk_id)
        result = self.dmg.storage_query_device_health(disk_id)
        # Check if device state changed to EVICTED.
        if 'State:EVICTED' not in result.stdout_text:
            self.fail("device State {} on host {} suppose to be EVICTED"
                      .format(disk_id, server))
        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True)
        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

    def set_device_faulty_loop(self):
        """Set devices to Faulty one by one and wait for rebuild to complete."""
        # Get the device ids from all servers and try to eject the disks
        device_ids = get_device_ids(self.dmg, self.hostlist_servers)

        # no_of_servers and no_of_drives can be set from test yaml.
        # 1 Server, 1 Drive = Remove single drive from single server
        for num in range(0, self.no_of_servers):
            server = self.hostlist_servers[num]
            for disk_id in range(0, self.no_of_drives):
                self.set_device_faulty(server, device_ids[server][disk_id])

    def get_max_storage_sizes(self):
        """Get the maximum pool sizes for the current server configuration.

        Returns:
            list: a list of the maximum SCM and NVMe size

        """
        try:
            sizes_dict = self.server_managers[0].get_available_storage()
            sizes = [sizes_dict["scm"], sizes_dict["nvme"]]
        except (ServerFailed, KeyError) as error:
            self.fail(error)

        # Return the 96% of storage space as it won't be used 100%
        # for pool creation.
        for index, _size in enumerate(sizes):
            sizes[index] = int(sizes[index] * 0.96)

        return sizes

    def create_pool_max_size(self, scm=False, nvme=False):
        """Create a single pool with Maximum NVMe/SCM size available.

        Args:
            scm (bool): To create the pool with max SCM size or not.
            nvme (bool): To create the pool with max NVMe size or not.

        Note: Method to Fill up the server. It will get the maximum Storage
              space and create the pool.
              Replace with dmg options in future when it's available.
        """
        # Create a pool
        self.add_pool(create=False)

        if nvme or scm:
            sizes = self.get_max_storage_sizes()

        # If NVMe is True get the max NVMe size from servers
        if nvme:
            self.pool.nvme_size.update('{}'.format(sizes[1]))

        # If SCM is True get the max SCM size from servers
        if scm:
            self.pool.scm_size.update('{}'.format(sizes[0]))

        # Create the Pool
        self.pool.create()

    def start_ior_load(self, storage='NVMe', operation="Write", percent=1,
                       create_cont=True):
        """Fill up the server either SCM or NVMe.

        Fill up based on percent amount given using IOR.

        Args:
            storage (string): SCM or NVMe, by default it will fill NVMe.
            operation (string): Write/Read operation
            percent (int): % of storage to be filled
            create_cont (bool): To create the new container for IOR
        """
        self.capacity = percent
        # Fill up NVMe by default
        self.nvme_fill = 'NVMe' in storage
        self.scm_fill = 'SCM' in storage

        # Create the IOR threads
        job = threading.Thread(target=self.start_ior_thread,
                               kwargs={"results": self.out_queue,
                                       "create_cont": create_cont,
                                       "operation": operation})
        # Launch the IOR thread
        job.start()

        # Set NVMe device faulty if it's set
        if self.set_faulty_device:
            time.sleep(60)
            # Set the device faulty
            self.set_device_faulty_loop()

        # Kill the server rank while IOR in progress
        if self.set_online_rebuild:
            time.sleep(30)
            # Kill the server rank
            if self.rank_to_kill is not None:
                self.get_dmg_command().system_stop(True, self.rank_to_kill)

        # Wait to finish the thread
        job.join()

        # Verify the queue and make sure no FAIL for any IOR run
        while not self.out_queue.empty():
            if self.out_queue.get() == "FAIL":
                self.fail("FAIL")
