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
from apricot import TestWithServers
from command_utils_base import CommandFailure
from job_manager_utils import Mpirun
from macsio_util import MacsioCommand


class MacsioTestBase(TestWithServers):
    """Base MACSio test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a MacsioTestBase object."""
        super(MacsioTestBase, self).__init__(*args, **kwargs)
        self.manager = None
        self.macsio = None

    def setUp(self):
        """Set up each test case."""
        super(MacsioTestBase, self).setUp()

        # Support using different job managers to launch the daos agent/servers
        mpi_type = self.params.get("mpi_type", default="mpich")
        self.manager = Mpirun(None, subprocess=False, mpitype=mpi_type)
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

    def run_macsio(self, pool_uuid, pool_svcl, cont_uuid=None, plugin=None):
        """Run the macsio.

        Parameters for the macsio command are obtained from the test yaml file,
        including the path to the macsio executable.

        By default mpirun will be used to run macsio.  This can be overridden by
        redfining the self.manager attribute prior to calling this method.

        Args:
            pool_uuid (str): pool uuid
            pool_svcl (str): pool service replica
            cont_uuid (str, optional): container uuid. Defaults to None.
            plugin (str, optional): plugin path to use with DAOS VOL connector

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        env = self.macsio.get_environment(
            self.server_managers[0], self.client_log)
        if plugin:
            # Include DAOS VOL environment settings
            env["HDF5_VOL_CONNECTOR"] = "daos"
            env["HDF5_PLUGIN_PATH"] = "{}".format(plugin)

        # Setup the job manager (mpirun) to run the macsio command
        self.macsio.daos_pool = pool_uuid
        self.macsio.daos_svcl = pool_svcl
        self.macsio.daos_cont = cont_uuid
        self.manager.job = self.macsio
        self.manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        self.manager.assign_processes(len(self.hostlist_clients))
        self.manager.assign_environment(env)
        try:
            return self.manager.run()

        except CommandFailure as error:
            self.log.error("MACSio Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
