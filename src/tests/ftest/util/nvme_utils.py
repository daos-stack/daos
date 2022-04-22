#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import threading
import re
import time
from exception_utils import CommandFailure
from avocado.core.exceptions import TestFail
from ior_test_base import IorTestBase
from server_utils import ServerFailed
from ior_utils import IorCommand
from job_manager_utils import get_job_manager
from daos_utils import DaosCommand
from test_utils_container import TestContainer

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
            raise CommandFailure("dmg list-devices failed with error {}".format(_error)) from _error
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
        self.fail_on_warning = False
        self.ior_matrix = None
        self.ior_local_cmd = None
        self.result = []
        self.nvme_local_cont = None

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()
        # Start the servers and agents
        super().setUp()
        self.hostfile_clients = None
        self.ior_local_cmd = IorCommand()
        self.ior_local_cmd.get_params(self)
        self.ior_default_flags = self.ior_local_cmd.flags.value
        self.ior_scm_xfersize = self.params.get("transfer_size",
                                                '/run/ior/transfersize_blocksize/*', '2048')
        self.ior_read_flags = self.params.get("read_flags", '/run/ior/iorflags/*', '-r -R -k -G 1')
        self.ior_nvme_xfersize = self.params.get("nvme_transfer_size",
                                                 '/run/ior/transfersize_blocksize/*', '16777216')
        # Get the number of daos_engine
        self.engines = self.server_managers[0].manager.job.yaml.engine_params
        self.dmg_command = self.get_dmg_command()

    def create_container(self):
        """Create the container """
        # Get container params
        self.nvme_local_cont = TestContainer(self.pool, daos_command=DaosCommand(self.bin))
        self.nvme_local_cont.get_params(self)

        # update container oclass
        if self.ior_local_cmd.dfs_oclass:
            self.nvme_local_cont.oclass.update(self.ior_local_cmd.dfs_oclass.value)

        self.nvme_local_cont.create()

    def start_ior_thread(self, create_cont, operation):
        """Start IOR write/read threads and wait until all threads are finished.

        Args:
            create_cont (Bool): To create the new container or not.
            operation (str):
                Write/WriteRead: It will Write or Write/Read base on IOR parameter in yaml file.
                Auto_Write/Auto_Read: It will calculate the IOR block size based on requested
                                        storage % to be fill.
        """
        # IOR flag can be Write only or Write/Read based on test yaml
        self.ior_local_cmd.flags.value = self.ior_default_flags

        # Calculate the block size based on server % to fill up.
        if 'Auto' in operation:
            block_size = self.calculate_ior_block_size()
            self.ior_local_cmd.block_size.update('{}'.format(block_size))

        # IOR Read operation update the read only flag from yaml file.
        if 'Auto_Read' in operation or operation == "Read":
            create_cont = False
            self.ior_local_cmd.flags.value = self.ior_read_flags

        self.ior_local_cmd.set_daos_params(self.server_group, self.pool)
        self.ior_local_cmd.test_file.update('/testfile')

        # Created new container or use the existing container for reading
        if create_cont:
            self.create_container()
        self.ior_local_cmd.dfs_cont.update(self.nvme_local_cont.uuid)

        # Define the job manager for the IOR command
        job_manager_main = get_job_manager(self, "Mpirun", self.ior_local_cmd, mpi_type="mpich")
        env = self.ior_local_cmd.get_default_env(str(job_manager_main))
        job_manager_main.assign_hosts(self.hostlist_clients, self.workdir, None)
        job_manager_main.assign_environment(env, True)
        job_manager_main.assign_processes(self.params.get("np", '/run/ior/client_processes/*'))

        # run IOR Command
        try:
            output = job_manager_main.run()
            self.ior_matrix = IorCommand.get_ior_metrics(output)

            for line in output.stdout_text.splitlines():
                if 'WARNING' in line and self.fail_on_warning:
                    self.result.append("FAIL-IOR command issued warnings.")

        except (CommandFailure, TestFail) as _error:
            self.result.append("FAIL")

    def calculate_ior_block_size(self):
        """Calculate IOR Block size to fill up the Server.

        Returns:
            block_size(int): IOR Block size

        """
        if self.scm_fill:
            free_space = self.pool.get_pool_daos_space()["s_total"][0]
            self.ior_local_cmd.transfer_size.value = self.ior_scm_xfersize
        elif self.nvme_fill:
            free_space = self.pool.get_pool_daos_space()["s_total"][1]
            self.ior_local_cmd.transfer_size.value = self.ior_nvme_xfersize
        else:
            self.fail('Provide storage type (SCM/NVMe) to be filled')

        # Get the block size based on the capacity to be filled. For example
        # If nvme_free_space is 100G and to fill 50% of capacity.
        # Formula : (107374182400 / 100) * 50.This will give 50%(50G) of space to be filled.
        _tmp_block_size = ((free_space/100)*self.capacity)

        # Check the IOR object type to calculate the correct block size.
        _replica = re.findall(r'_(.+?)G', self.ior_local_cmd.dfs_oclass.value)

        # This is for non replica and EC class where _tmp_block_size will not change.
        if not _replica:
            pass

        # If it's EC object, Calculate the tmp block size based on number of data + parity
        # targets. And calculate the write data size for the total number data targets.
        # For example: 100Gb of total pool to be filled 10% in total: For EC_4P1GX,  Get the data
        # target fill size = 8G, which will fill 8G of data and 2G of Parity. So total 10G (10%
        # of 100G of pool size)
        elif 'P' in _replica[0]:
            replica_server = re.findall(r'\d+', _replica[0])[0]
            parity_count = re.findall(r'\d+', _replica[0])[1]
            _tmp_block_size = int(_tmp_block_size / (int(replica_server) + int(parity_count)))
            _tmp_block_size = int(_tmp_block_size) * int(replica_server)

        # This is Replica type object class
        else:
            _tmp_block_size = int(_tmp_block_size / int(_replica[0]))

        # Last divide the Total sized with IOR number of process
        _tmp_block_size = int(_tmp_block_size) / self.processes

        # Calculate the Final block size of IOR multiple of Transfer size.
        block_size = (int(_tmp_block_size / int(self.ior_local_cmd.transfer_size.value)) * int(
            self.ior_local_cmd.transfer_size.value))

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
            self.fail("device State {} on host {} suppose to be EVICTED".format(disk_id, server))

        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True)
        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

    def set_device_faulty_loop(self):
        """Set devices to Faulty one by one and wait for rebuild to complete."""
        # Get the device ids from all servers and try to eject the disks
        device_ids = get_device_ids(self.dmg, self.hostlist_servers)

        # no_of_servers and no_of_drives can be set from test yaml. 1 Server, 1 Drive = Remove
        # single drive from single server
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

        # Return the 96% of storage space as it won't be used 100% for pool creation.
        for index, _size in enumerate(sizes):
            sizes[index] = int(sizes[index] * 0.96)

        return sizes

    def create_pool_max_size(self, scm=False, nvme=False):
        """Create a single pool with Maximum NVMe/SCM size available.

        Args:
            scm (bool): To create the pool with max SCM size or not.
            nvme (bool): To create the pool with max NVMe size or not.

        Note: Method to Fill up the server. It will get the maximum Storage space and create the
              pool. Replace with dmg options in future when it's available.
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

    def start_ior_load(self, storage='NVMe', operation="WriteRead",
                       percent=1, create_cont=True):
        """Fill up the server either SCM or NVMe.

        Fill up based on percent amount given using IOR.

        Args:
            storage (string): SCM or NVMe, by default it will fill NVMe.
            operation (string): Write/Read operation
            percent (int): % of storage to be filled
            create_cont (bool): To create the new container for IOR
        """
        self.result.clear()
        self.capacity = percent
        # Fill up NVMe by default
        self.nvme_fill = 'NVMe' in storage
        self.scm_fill = 'SCM' in storage

        # Create the IOR threads
        job = threading.Thread(target=self.start_ior_thread, kwargs={"create_cont": create_cont,
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
                self.server_managers[0].stop_ranks([self.rank_to_kill], self.d_log, force=True)

        # Wait to finish the thread
        job.join()

        # Verify if any test failed for any IOR run
        for test_result in self.result:
            if "FAIL" in test_result:
                self.fail(test_result)
