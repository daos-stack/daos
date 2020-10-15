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
The Government's rights to use, modify, reproduce, release, perform,
display, or disclose this software are subject to the terms of the Apache
License as provided in Contract No. B609815.
Any reproduction of computer software, computer software documentation, or
portions thereof marked with this legend must also reproduce the markings.
"""
from dfuse_test_base import DfuseTestBase
from command_utils_base import EnvironmentVariables, CommandFailure
from hdf5_vol_utils import Hdf5VolCommand


class Hdf5VolTestBase(DfuseTestBase):
    # pylint: disable=too-few-public-methods,too-many-ancestors
    """Runs HDF5 vol test suites.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Hdf5VolTestBase object."""
        super(Hdf5VolTestBase, self).__init__(*args, **kwargs)
        self.hdf5_vol_cmd = None

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super(Hdf5VolTestBase, self).setUp()

        # Get the parameters for the HDF5 VOL command
        hdf5_vol_repo = self.params.get("hdf5_vol_repo")
        self.hdf5_vol_cmd = Hdf5VolCommand(hdf5_vol_repo)
        self.hdf5_vol_cmd.get_params(self)

    def run_test(self):
        """Run the HDF5 VOL testsuites.

        Raises:
            VolFailed: for an invalid test name or test execution failure

        """
        # initialize test specific variables
        plugin_path = self.params.get("plugin_path")
        client_processes = self.params.get("client_processes")

        # create pool, container and dfuse mount
        self.add_pool(connect=False)
        self.add_container(self.pool)

        # VOL needs to run from a file system that supports xattr.
        #  Currently nfs does not have this attribute so it was recommended
        #  to create a dfuse dir and run vol tests from there.
        # create dfuse container
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        # Assign the test to run
        self.job_manager.job = self.hdf5_vol_cmd

        env = EnvironmentVariables()
        env["DAOS_POOL"] = "{}".format(self.pool.uuid)
        env["DAOS_SVCL"] = "{}".format(self.pool.svc_ranks[0])
        env["DAOS_CONT"] = "{}".format(self.container.uuid)
        env["HDF5_VOL_CONNECTOR"] = "daos"
        env["HDF5_PLUGIN_PATH"] = "{}".format(plugin_path)
        self.job_manager.assign_hosts(self.hostlist_clients)
        self.job_manager.assign_processes(client_processes)
        self.job_manager.assign_environment(env, True)
        self.job_manager.working_dir.value = self.dfuse.mount_dir.value

        # Run the HDF5 test with the VOL connector
        try:
            self.job_manager.run()
        except CommandFailure as _error:
            self.fail("{} FAILED> \nException occurred: {}".format(
                self.job_manager.job, str(_error)))
