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
from data_mover_utils import DataMover
from command_utils_base import CommandFailure

# pylint: disable=too-many-ancestors
class ContainerCopy(MdtestBase, IorTestBase):
    """Test class Description: Add datamover test to copy large data amongst
                               daos containers and external file system.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initializing ContainerCopy object"""
        super(ContainerCopy, self).__init__(*args, **kwargs)

        self.container = []

    def create_cont(self):
        """Create a TestContainer object to be used to create container."""
        # Get container params and create
        self.container.append(self.get_container(self.pool, create=False))
        self.container[-1].create()

    def run_dcp(self, src_pool=None, dst_pool=None, src_cont=None,
                dst_cont=None, update_dest=False):
        """Initialize and run DatamoverCommand object

          Args:
            src_pool(TestPool): source pool object
            dst_pool(TestPool): destination pool object
            src_cont(TestContainer): source container object
            dst_cont(TestContainer): destination container object
            update_dest(bool): Update destination path

          Raise:
            raises Commandfailure
        """
        # param for dcp processes
        processes = self.params.get("processes", "/run/datamover/*")

        dcp = DataMover(self.hostlist_clients)
        dcp.get_params(self)
        # update dest path
        if update_dest:
            dcp.dest_path.update(self.dfuse.mount_dir.value)
        # set datamover params
        dcp.set_datamover_params(src_pool, dst_pool, src_cont, dst_cont)

        try:
            # run dcp
            dcp.run(self.workdir, processes)

        except CommandFailure as error:
            self.log.error("DCP command failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")

    def test_daoscont1_to_daoscont2(self):
        """Jira ID: DAOS-4782.
        Test Description:
            Copy data from daos cont1 to daos cont2
        Use Cases:
            Create a pool.
            Create POSIX type container.
            Run mdtest -a DFS to create 50K files of size 4K
            Create second container
            Copy data from cont1 to cont2 using dcp
            Run mdtest again, but this time on cont2 and read the files back.
        :avocado: tags=all,datamover,hw,large,full_regression
        :avocado: tags=containercopy,daoscont1_to_daoscont2
        """

        # test params
        mdtest_flags = self.params.get("mdtest_flags", "/run/mdtest/*")
        file_size = self.params.get("bytes", "/run/mdtest/*")
        # create pool and cont
        self.add_pool(connect=False)
        self.create_cont()

        # set and update mdtest command params
        self.mdtest_cmd.set_daos_params(self.server_group, self.pool,
                                        self.container[0].uuid)
        self.mdtest_cmd.flags.update(mdtest_flags[0])
        self.mdtest_cmd.write_bytes.update(file_size)
        # run mdtest
        self.run_mdtest(self.get_mdtest_job_manager_command(self.manager),
                        self.processes)
        # create second container
        self.create_cont()

        # copy data from cont1 to cont2
        self.run_dcp(self.pool, self.pool, self.container[0], self.container[1])

        # update and run mdtest read on cont2
        self.mdtest_cmd.set_daos_params(self.server_group, self.pool,
                                        self.container[1].uuid)
        self.mdtest_cmd.flags.update(mdtest_flags[1])
        self.mdtest_cmd.read_bytes.update(file_size)

        self.run_mdtest(self.get_mdtest_job_manager_command(self.manager),
                        self.processes)

    def test_daoscont_to_posixfs(self):
        """Jira ID: DAOS-4782.
        Test Description:
            Copy data from daos container to external Posix file system
        Use Cases:
            Create a pool
            Create POSIX type container.
            Run ior -a DFS on cont1.
            Create cont2
            Copy data from cont1 to cont2 using dcp.
            Copy data from cont2 to external Posix File system.
            (Assuming dfuse mount as external POSIX FS)
            Run ior -a DFS with read verify on copied directory to verify
            data.
        :avocado: tags=all,datamover,hw,large,full_regression
        :avocado: tags=containercopy,daoscont_to_posixfs
        """

        # create pool and cont
        self.add_pool(connect=False)
        self.create_cont()

        # update and run ior on cont1
        self.ior_cmd.set_daos_params(self.server_group, self.pool,
                                     self.container[0].uuid)
        self.run_ior(self.get_ior_job_manager_command(), self.processes)

        # create cont2
        self.create_cont()

        # copy from daos cont1 to cont2
        self.run_dcp(self.pool, self.pool, self.container[0], self.container[1])

        # start dfuse
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        # copy from daos cont to posix file system
        self.run_dcp(self.pool, None, self.container[1], None, True)

        # update ior params, read back and verify data from posix file system
        self.ior_cmd.api.update("POSIX")
        self.ior_cmd.flags.update("-r -R")
        dest_path = self.dfuse.mount_dir.value + self.ior_cmd.test_file.value
        self.ior_cmd.test_file.update(dest_path)
        self.ior_cmd.set_daos_params(self.server_group, self.pool)
        self.run_ior(self.get_ior_job_manager_command(), self.processes)
