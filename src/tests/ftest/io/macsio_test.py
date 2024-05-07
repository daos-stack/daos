"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from command_utils_base import CommandFailure
from dfuse_utils import get_dfuse, start_dfuse
from general_utils import get_log_file, list_to_str
from job_manager_utils import get_job_manager
from macsio_util import MacsioCommand


class MacsioTest(TestWithServers):
    """Test class Description: Runs a basic MACSio test.

    :avocado: recursive
    """

    def get_macsio_command(self, pool_uuid, pool_svcl, cont_uuid):
        """Get the MacsioCommand object.

        Args:
            pool_uuid (str): pool uuid
            pool_svcl (str): pool service replica
            cont_uuid (str, optional): container uuid. Defaults to None.

        Returns:
            MacsioCommand: object defining the macsio command
        """
        # Create the macsio command
        path = self.params.get("macsio_path", "/run/job_manager/*", default="")
        macsio = MacsioCommand(path)
        macsio.get_params(self)

        # Create all the macsio output files in the same directory as the other test log files
        macsio.set_output_file_path()

        # Update the MACSio pool and container info before gathering manager
        # environment information to ensure they are included.
        macsio.daos_pool = pool_uuid
        macsio.daos_svcl = pool_svcl
        macsio.daos_cont = cont_uuid

        return macsio

    def run_macsio(self, macsio, hosts, processes, plugin=None, slots=None, working_dir=None):
        """Run the macsio test.

        Parameters for the macsio command are obtained from the test yaml file,
        including the path to the macsio executable.

        Args:
            macsio (MacsioCommand): object defining the macsio command
            hosts (NodeSet): hosts on which to run macsio
            processes (int): total number of processes to use to run macsio
            plugin (str, optional): plugin path to use with DAOS VOL connector
            slots (int, optional): slots per host to specify in the hostfile. Defaults to None.
            working_dir (str, optional): working directory. Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other information.
        """
        # Setup the job manager to run the macsio command
        env = macsio.env.copy()
        env["D_LOG_FILE"] = get_log_file(self.client_log or "{}_daos.log".format(macsio.command))
        if plugin:
            # Include DAOS VOL environment settings
            env["HDF5_VOL_CONNECTOR"] = "daos"
            env["HDF5_PLUGIN_PATH"] = str(plugin)
        job_manager = get_job_manager(self)
        job_manager.job = macsio
        job_manager.assign_hosts(hosts, self.workdir, slots)
        job_manager.assign_processes(processes)
        job_manager.assign_environment(env)
        job_manager.working_dir.value = working_dir

        # Run MACSio
        result = None
        try:
            result = job_manager.run()

        except CommandFailure as error:
            self.log.error("MACSio Failed: %s", str(error))
            self.fail("MACSio Failed")

        return result

    def test_macsio(self):
        """JIRA ID: DAOS-3658.

        Test Description:
            Purpose of this test is to check basic functionality for DAOS,
            MPICH, HDF5, and MACSio.

        Use case:
            Six clients and two servers.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=io,macsio,dfuse
        :avocado: tags=MacsioTest,test_macsio
        :avocado: tags=DAOS_5610
        """
        processes = self.params.get("processes", "/run/macsio/*", len(self.hostlist_clients))

        # Create a pool
        self.log_step('Create a single pool')
        pool = self.get_pool()
        pool.display_pool_daos_space()

        # Create a container
        self.log_step('Create a single container')
        container = self.get_container(pool)

        # Run macsio
        self.log_step("Running MACSio")
        macsio = self.get_macsio_command(pool.uuid, list_to_str(pool.svc_ranks), container.uuid)
        result = self.run_macsio(macsio, self.hostlist_clients, processes)
        if not macsio.check_results(result, self.hostlist_clients):
            self.fail("MACSio failed")
        self.log.info("Test passed")

    def test_macsio_daos_vol(self):
        """JIRA ID: DAOS-4983.

        Test Description:
            Purpose of this test is to check basic functionality for DAOS,
            MPICH, HDF5, and MACSio with DAOS VOL connector.

        Use case:
            Six clients and two servers.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=io,macsio,dfuse,daos_vol
        :avocado: tags=MacsioTest,test_macsio_daos_vol
        :avocado: tags=DAOS_5610
        """
        plugin_path = self.params.get("plugin_path", "/run/job_manager/*")
        processes = self.params.get("processes", "/run/macsio/*", len(self.hostlist_clients))

        # Create a pool
        self.log_step('Create a single pool')
        pool = self.get_pool()
        pool.display_pool_daos_space()

        # Create a container
        self.log_step('Create a single container')
        container = self.get_container(pool)

        # Create dfuse mount point
        self.log_step('Starting dfuse')
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        # VOL needs to run from a file system that supports xattr.  Currently
        # nfs does not have this attribute so it was recommended to create and
        # use a dfuse dir and run vol tests from there.
        # self.job_manager.working_dir.value = dfuse.mount_dir.value

        # Run macsio
        self.log_step("Running MACSio with DAOS VOL connector")
        macsio = self.get_macsio_command(pool.uuid, list_to_str(pool.svc_ranks), container.uuid)
        result = self.run_macsio(
            macsio, self.hostlist_clients, processes, plugin_path,
            working_dir=dfuse.mount_dir.value)
        if not macsio.check_results(result, self.hostlist_clients):
            self.fail("MACSio failed")
        self.log.info("Test passed")
