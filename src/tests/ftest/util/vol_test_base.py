#!/usr/bin/python
'''
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
    '''
from __future__ import print_function

import os

from apricot import TestWithServers
from command_utils_base import EnvironmentVariables, CommandFailure
from job_manager_utils import Mpirun, Orterun
from dfuse_utils import Dfuse
from ClusterShell.NodeSet import NodeSet


class VolTestBase(TestWithServers):
    """

    Runs HDF5 vol test suites.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super(VolTestBase, self).__init__(*args, **kwargs)
        self.dfuse = None

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.dfuse:
                self.dfuse.stop()
        finally:
            # Stop the servers and agents
            super(VolTestBase, self).tearDown()

    def _start_dfuse(self):
        """Create a DfuseCommand object to start dfuse."""
        self.dfuse = Dfuse(self.hostlist_clients, self.tmp)
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.set_dfuse_params(self.pool)
        self.dfuse.set_dfuse_cont_param(self.container)
        self.dfuse.set_dfuse_exports(self.server_managers[0], self.client_log)

        try:
            # start dfuse
            self.dfuse.run()
        except CommandFailure as error:
            self.log.error("Dfuse command %s failed on hosts %s",
                           str(self.dfuse),
                           str(NodeSet.fromlist(self.dfuse.hosts)),
                           exc_info=error)
            self.fail("Test was expected to pass but it failed.\n")

    def run_test(self):
        """Run the HDF5 VOL testsuites.

        Raises:
            VolFailed: for an invalid test name or test execution failure

        """
        # initialize test specific variables
        mpi_type = self.params.get("mpi_type", default="mpich")
        test_repo = self.params.get("daos_vol_repo")
        plugin_path = self.params.get("plugin_path")
        # test_list = self.params.get("daos_vol_tests", default=[])
        testname = self.params.get("testname")
        client_processes = self.params.get("client_processes")

        # create pool, container and dfuse mount
        self.add_pool(connect=False)
        self.add_container(self.pool)

        # VOL needs to run from a file system that supports xattr.
        #  Currently nfs does not have this attribute so it was recommended
        #  to create a dfuse dir and run vol tests from there.
        # create dfuse container
        self._start_dfuse()

        # for test_param in test_list:
        # testname = test_param[0][1]
        # client_processes = test_param[1][1]
        exe = os.path.join(test_repo, testname)
        if mpi_type == "openmpi":
            manager = Orterun(exe, subprocess=False)
        else:
            manager = Mpirun(exe, subprocess=False, mpitype="mpich")

        env = EnvironmentVariables()
        env["DAOS_POOL"] = "{}".format(self.pool.uuid)
        env["DAOS_SVCL"] = "{}".format(self.pool.svc_ranks[0])
        env["DAOS_CONT"] = "{}".format(self.container.uuid)
        env["HDF5_VOL_CONNECTOR"] = "daos"
        env["HDF5_PLUGIN_PATH"] = "{}".format(plugin_path)
        manager.assign_hosts(self.hostlist_clients)
        manager.assign_processes(client_processes)
        manager.assign_environment(env, True)
        manager.wdir.value = self.dfuse.mount_dir.value

        # run VOL Command
        try:
            manager.run()
        except CommandFailure as _error:
            self.fail("{} FAILED> \nException occurred: {}".format(
                exe, str(_error)))
