#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from command_utils_base import CommandFailure
from macsio_util import MacsioCommand


class MacsioTestBase(TestWithServers):
    """Base MACSio test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a MacsioTestBase object."""
        super(MacsioTestBase, self).__init__(*args, **kwargs)
        self.macsio = None

    def setUp(self):
        """Set up each test case."""
        super(MacsioTestBase, self).setUp()
        self.macsio = self.get_macsio_command()

    def get_macsio_command(self):
        """Get the MacsioCommand object.

        Returns:
            MacsioCommand: object defining the macsio command

        """
        # Create the macsio command
        path = self.params.get("macsio_path", default="")
        macsio = MacsioCommand(path)
        macsio.get_params(self)
        # Create all the macsio output files in the same directory as the other
        # test log files
        macsio.set_output_file_path()

        return macsio

    def run_macsio(self, pool_uuid, pool_svcl, cont_uuid=None, plugin=None,
                   slots=None):
        """Run the macsio test.

        Parameters for the macsio command are obtained from the test yaml file,
        including the path to the macsio executable.

        Args:
            pool_uuid (str): pool uuid
            pool_svcl (str): pool service replica
            cont_uuid (str, optional): container uuid. Defaults to None.
            plugin (str, optional): plugin path to use with DAOS VOL connector
            slots (int, optional): slots per host to specify in the hostfile.
                Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        # Update the MACSio pool and container info before gathering manager
        # environment information to ensure they are included.
        self.macsio.daos_pool = pool_uuid
        self.macsio.daos_svcl = pool_svcl
        self.macsio.daos_cont = cont_uuid

        # Setup the job manager to run the macsio command
        env = self.macsio.get_environment(
            self.server_managers[0], self.client_log)
        if plugin:
            # Include DAOS VOL environment settings
            env["HDF5_VOL_CONNECTOR"] = "daos"
            env["HDF5_PLUGIN_PATH"] = "{}".format(plugin)
        self.job_manager.job = self.macsio
        self.job_manager.assign_hosts(
            self.hostlist_clients, self.workdir, slots)
        self.job_manager.assign_processes(len(self.hostlist_clients))
        self.job_manager.assign_environment(env)

        # Run MACSio
        try:
            return self.job_manager.run()

        except CommandFailure as error:
            self.log.error("MACSio Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
