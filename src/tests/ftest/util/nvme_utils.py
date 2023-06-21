"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import threading
import re
import time

from avocado import fail_on
from avocado.core.exceptions import TestFail

from dmg_utils import get_storage_query_device_uuids, get_dmg_response
from exception_utils import CommandFailure
from ior_test_base import IorTestBase
from ior_utils import IorCommand
from job_manager_utils import get_job_manager
from server_utils import ServerFailed


def get_device_ids(dmg, servers):
    """Get the NVMe Device ID from servers.

    Args:
        dmg (DmgCommand): a DmgCommand class instance.
        servers (list): list of server hosts.

    Raises:
        CommandFailure: if there is an error obtaining the NVMe Device ID

    Returns:
        devices (dictionary): Device UUID for servers.

    """
    devices = {}
    for host in servers:
        dmg.hostlist = host
        devices[host] = []
        for uuid_list in get_storage_query_device_uuids(dmg).values():
            devices[host].extend(uuid_list)
    return devices


def set_device_faulty(test, dmg, server, uuid, pool=None, **kwargs):
    """Set the device faulty and optionally wait for rebuild to complete.

    Args:
        test (Test): avocado test class
        dmg (DmgCommand): a DmgCommand class instance
        server (NodeSet): host on which to issue the dmg storage set nvme-faulty
        uuid (str): the device UUID
        pool (TestPool, optional): pool used to wait for rebuild to start/complete if specified.
            Defaults to None.
        kwargs (dict, optional): named arguments to pass to the DmgCommand.storage_set_faulty.

    Returns:
        dict: the json response from the dmg storage set-faulty command.

    """
    dmg.hostlist = server
    kwargs['uuid'] = uuid
    try:
        response = get_dmg_response(dmg.storage_set_faulty, **kwargs)
    except CommandFailure as error:
        test.fail(str(error))

    # Add a tearDown method to reset the faulty device
    test.register_cleanup(reset_fault_device, dmg=dmg, server=server, uuid=uuid)

    if pool:
        # Wait for rebuild to start
        pool.wait_for_rebuild_to_start()
        # Wait for rebuild to complete
        pool.wait_for_rebuild_to_end()

    return response


def reset_fault_device(dmg, server, uuid):
    """Call dmg storage led identify to reset the device.

    Args:
        dmg (DmgCommand): a DmgCommand class instance
        server (NodeSet): host on which to issue the dmg storage led identify
        uuid (str): device to reset

    Returns:
        list: a list of any errors detected when removing the pool

    """
    error_list = []
    dmg.hostlist = server
    try:
        get_dmg_response(dmg.storage_led_identify, reset=True, ids=uuid)
    except CommandFailure as error:
        error_list.append("Error resetting device {}: {}".format(uuid, error))
    return error_list


class ServerFillUp(IorTestBase):
    """Class to fill up the servers based on pool percentage given.

    It will get the drives listed in yaml file and find the maximum capacity of
    the pool which will be created.
    IOR block size will be calculated as part of function based on percentage
    of pool needs to fill up.
    """

    def __init__(self, *args, **kwargs):
        """Initialize a IorTestBase object."""
        super().__init__(*args, **kwargs)
        self.capacity = 1
        self.no_of_servers = 1
        self.no_of_drives = 1
        self.pool = None
        self.dmg = None
        self.set_faulty_device = False
        self.set_online_rebuild = False
        self.scm_fill = False
        self.nvme_fill = False
        self.ior_matrix = None
        self.ior_local_cmd = None
        self.result = []
        self.fail_on_warning = False
        self.rank_to_kill = []
        self.pool_exclude = {}
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
        """Create the container."""
        self.nvme_local_cont = self.get_container(self.pool, create=False)

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
        # IOR flag can Write/Read based on test yaml
        self.ior_local_cmd.flags.value = self.ior_default_flags

        # Calculate the block size based on server % to fill up.
        if 'Auto' in operation:
            block_size = self.calculate_ior_block_size()
            self.ior_local_cmd.block_size.update('{}'.format(block_size))

        # For IOR Read operation update the read only flag from yaml file.
        if 'Auto_Read' in operation or operation == "Read":
            create_cont = False
            self.ior_local_cmd.flags.value = self.ior_read_flags

        self.ior_local_cmd.set_daos_params(self.server_group, self.pool, None)
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
        except (CommandFailure, TestFail) as error:
            self.result.append("FAIL - {}".format(error))

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
        _tmp_block_size = (free_space / 100) * self.capacity

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

    @fail_on(CommandFailure)
    def set_device_faulty_loop(self):
        """Set devices to Faulty one by one and wait for rebuild to complete."""
        # Get the device ids from all servers and try to eject the disks
        device_ids = get_device_ids(self.dmg, self.hostlist_servers)

        # no_of_servers and no_of_drives can be set from test yaml. 1 Server, 1 Drive = Remove
        # single drive from single server
        for num in range(0, self.no_of_servers):
            server = self.hostlist_servers[num]
            for disk_id in range(0, self.no_of_drives):
                set_device_faulty(self, self.dmg, server, device_ids[server][disk_id], self.pool)

    def get_max_storage_sizes(self, percentage=96):
        """Get the maximum pool sizes for the current server configuration.

        Args:
            percentage (int): percentage of the maximum storage space to use

        Returns:
            list: a list of the maximum SCM and NVMe size

        """
        try:
            sizes_dict = self.server_managers[0].get_available_storage()
            # Return a percentage of the storage space as it won't be used 100% for pool creation.
            percentage /= 100
            sizes = [int(sizes_dict["scm"] * percentage), int(sizes_dict["nvme"] * percentage)]
        except (ServerFailed, KeyError, ValueError) as error:
            self.fail(error)

        return sizes

    def create_pool_max_size(self, scm=False, nvme=False, percentage=96):
        """Create a single pool with Maximum NVMe/SCM size available.

        Args:
            scm (bool): To create the pool with max SCM size or not.
            nvme (bool): To create the pool with max NVMe size or not.
            percentage (int): percentage of the maximum storage space to use

        Note: Method to Fill up the server. It will get the maximum Storage space and create the
              pool. Replace with dmg options in future when it's available.
        """
        # Create a pool
        self.add_pool(create=False)

        if nvme or scm:
            sizes = self.get_max_storage_sizes(percentage)

        # If NVMe is True get the max NVMe size from servers
        if nvme:
            self.pool.nvme_size.update(str(sizes[1]))

        # If SCM is True get the max SCM size from servers
        if scm:
            self.pool.scm_size.update(str(sizes[0]))

        # Create the Pool
        self.pool.create()

    def kill_rank_thread(self, rank):
        """Server rank kill thread function.

        Args:
            rank: Rank number to kill the daos server
        """
        self.server_managers[0].stop_ranks([rank], self.d_log, force=True, copy=True)

    def exclude_target_thread(self, rank, target):
        """Target kill thread function.

        Args:
            rank(int): Rank number to kill the target from
            target(str): target number or range of targets to kill
        """
        self.pool.exclude(rank, str(target))

    def start_ior_load(self, storage='NVMe', operation="WriteRead", percent=1, create_cont=True):
        """Fill up the server either SCM or NVMe.

        Fill up based on percent amount given using IOR.

        Args:
            storage (string): SCM or NVMe, by default it will fill NVMe.
            operation (string): Write/Read operation
            percent (int): % of storage to be filled
            create_cont (bool): To create the new container for IOR
        """
        kill_rank_job = []
        kill_target_job = []
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
            # Kill the server rank in BG thread
            for _id, _rank in enumerate(self.rank_to_kill):
                kill_rank_job.append(threading.Thread(target=self.kill_rank_thread,
                                                      kwargs={"rank": _rank}))
                kill_rank_job[_id].start()

            # Kill the target from rank in BG thread
            for _id, (key, value) in enumerate(self.pool_exclude.items()):
                kill_target_job.append(threading.Thread(target=self.exclude_target_thread,
                                                        kwargs={"rank": key, "target": value}))
                kill_target_job[_id].start()

            # Wait for server kill thread to finish
            for _kill_rank in kill_rank_job:
                _kill_rank.join()

            # Wait for rank kill thread to finish
            for _kill_tgt in kill_target_job:
                _kill_tgt.join()

        # Wait to finish the IOR thread
        job.join()

        # Verify if any test failed for any IOR run
        for test_result in self.result:
            if "FAIL" in test_result:
                self.fail(test_result)
