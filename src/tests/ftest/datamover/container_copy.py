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

from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from test_utils_container import TestContainer
from daos_utils import DaosCommand
from data_mover_utils import DataMover

# pylint: disable=too-many-ancestors
class ContainerCopy(MdtestBase, IorTestBase):
    """Test class Description: Verify data consistency using different
                               middlewares. In this case, using DFS and POSIX.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialising ContainerCopy object"""
        super(ContainerCopy, self).__init__(*args, **kwargs)

        self.container = []
#    def tearDown(self):
#        """Tear down each test case."""
#        try:
#            if self.dfuse:
#                self.dfuse.stop()
#        finally:
#            # Stop the servers and agents
#            super(ContainerCopy, self).tearDown()


    def create_cont(self):
        """Create a TestContainer object to be used to create container."""
        # Get container params
        container = TestContainer(
            self.pool, daos_command=DaosCommand(self.bin))
        container.get_params(self)

        # create container
        container.create()

        self.container.append(container)

    def test_daoscont1_to_daoscont2(self):
        """Jira ID: DAOS-4778.
        Test Description:
            Run IOR first using DFS and then using POSIX to verify data
            consistency.
        Use Cases:
            Create a pool
            Create POSIX type container.
            Run ior -a DFS with FPP and keep the file.
            Mount the container using dfuse
            Run IOR -a POSIX -r -R to verify data consistency and delete the
            file.
            Try to re-create to the same file name after deletion, which should
            work without issues.
            Repeat the same steps as above for SSF this time.
        :avocado: tags=all,daosio,hw,large,pr,daoscont1_to_daoscont2
        """

        # test params
        processes = self.params.get("processes", "/run/datamover/*")

        self.create_pool()
        self.create_cont()

        self.execute_mdtest(self.container[0])

        self.create_cont()

        print("Container2: {}".format(self.container[1].uuid))
        
        dcp = DataMover(self.hostlist_clients, self.pool, self.pool, self.container[0], self.container[1])
        dcp.get_params(self)
        dcp.set_datamover_params()

        dcp.run(self.workdir, processes)
#        dcp.run(self.workdir, processes)

    def test_daoscont_to_posixfs(self):
        """Jira ID: DAOS-4778.
        Test Description:
            Run IOR first using DFS and then using POSIX to verify data
            consistency.
        Use Cases:
            Create a pool
            Create POSIX type container.
            Run ior -a DFS with FPP and keep the file.
            Mount the container using dfuse
            Run IOR -a POSIX -r -R to verify data consistency and delete the
            file.
            Try to re-create to the same file name after deletion, which should
            work without issues.
            Repeat the same steps as above for SSF this time.
        :avocado: tags=all,daosio,hw,large,pr,daoscont_to_posixfs
        """

        # test params
        processes = self.params.get("processes", "/run/datamover/*")

        self.create_pool()
        self.create_cont()

        self.ior_cmd.set_daos_params(self.server_group, self.pool,
                                     self.container[0].uuid)
        self.run_ior(self.get_ior_job_manager_command(), self.processes)


        self.create_cont()

        print("Container2: {}".format(self.container[1].uuid))

        # copy from daos cont to another daos cont
        dcp = DataMover(self.hostlist_clients, self.pool, self.pool, self.container[0], self.container[1])
        dcp.get_params(self)
        dcp.set_datamover_params()

        dcp.run(self.workdir, processes)

        # copy from daos cont to posix file system
        dcp = DataMover(self.hostlist_clients, self.pool, None, self.container[1])
        dcp.get_params(self)
        dcp.dest_path.update(self.tmp)
        dcp.set_datamover_params()
#        dcp.dest_path.update(self.tmp)

        dcp.run(self.workdir, processes)

        self.ior_cmd.api.update("POSIX")
        self.ior_cmd.flags.update("-r -R")
        dest_path = dcp.dest_path.value + self.ior_cmd.test_file.value
        cmd = u"chmod 755 {}".format(dest_path)
        self.execute_cmd(cmd)
        self.ior_cmd.test_file.update(dest_path)
        self.ior_cmd.set_daos_params(self.server_group, self.pool)
        self.run_ior(self.get_ior_job_manager_command(), self.processes)
