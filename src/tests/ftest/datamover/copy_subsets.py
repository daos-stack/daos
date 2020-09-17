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
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from command_utils import CommandFailure
from mdtest_test_base import MdtestBase
from ior_test_base import IorTestBase
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from data_mover_utils import DataMover
import os

    
class CopySubsetsTest(IorTestBase, MdtestBase):
    """
    Test Class Description:
        Tests ability to copy container subsets. 
        Tests the following cases:
            Copying between prefix subsets and UUID subsets.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
       	"""Initialize a CopyTypesTest object."""
        super(CopySubsetsTest, self).__init__(*args, **kwargs)
        self.pool = None
        self.next_daos_path_index = 1

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(CopySubsetsTest, self).setUp()
        
        # Get the parameters
        self.flags_write = self.params.get("flags_write", "/run/mdtest/copy_subsets/*")
        self.flags_read = self.params.get("flags_read", "/run/mdtest/copy_subsets/*")
        self.uns_dir = self.params.get("uns_dir", "/run/container/copy_subsets/*")

        # Create the directory
        cmd = "mkdir -p '{}'".format(
            self.uns_dir)
        self.execute_cmd(cmd)

    def tearDown(self):
        """Tear down each test case."""
        # Remove the created directory
        cmd = "rm -r '{}'".format(
            self.uns_dir)
        self.execute_cmd(cmd)
        
        # Stop the servers and agents
        super(CopySubsetsTest, self).tearDown()
    
    def create_cont(self, pool, path=None):
        """Create a TestContainer object."""
        # Get container params
        container = TestContainer(
            pool, daos_command=DaosCommand(self.bin))
        container.get_params(self)

        if path is not None:
            container.path.update(path)
        
        # Create container
        container.create()

        return container

    def next_daos_path(self):
        """Returns the next unique container path"""
        daos_path = "/test{}".format(str(self.next_daos_path_index))
        self.next_daos_path_index += 1
        return daos_path

    def test_copy_subsets(self):
        """
        Test Description:
            DAOS-5512: Verify ability to copy container subsets.
        Use Cases:
            Create a pool.
            Create POSIX container1 with a UNS path.
            Create a directory structure with a single 1K file in container1.
            Copy a prefix subset to a UUID subset.
            Copy a UUID subset to a prefix subset.
            Copy a UUID subset to a UUID subset.
        :avocado: tags=all,pr,datamover
        :avocado: tags=copy_subsets
        """
        # Create pool and container
        self.create_pool()
        uns1 = os.path.join(self.uns_dir, "uns1")
        container1 = self.create_cont(self.pool, uns1)

        # DAOS source paths and test data
        cont1_path1 = self.next_daos_path()
        uns1_path1 = uns1 + cont1_path1
        self.write_daos(self.pool, container1, cont1_path1)

        # (1) DAOS prefix subset to DAOS UUID subset
        dst_path = self.next_daos_path()
        self.run_dcp(
            src=uns1_path1, dst=dst_path,
            prefix=uns1,
            dst_pool=self.pool, dst_cont=container1,
            test_desc="copy_subsets (1)")
        self.read_verify_daos(self.pool, container1, dst_path)

        # (2) DAOS UUID subset to DAOS prefix subset
        dst_path = self.next_daos_path()
        uns_dst_path = uns1 + dst_path
        self.run_dcp(
            src=cont1_path1, dst=uns_dst_path,
            prefix=uns1,
            src_pool=self.pool, src_cont=container1,
            test_desc="copy_subsets (2)")
        self.read_verify_daos(self.pool, container1, dst_path)

        # (3) DAOS UUID subset to DAOS UUID subset
        dst_path = self.next_daos_path()
        self.run_dcp(
            src=cont1_path1, dst=dst_path,
            src_pool=self.pool, src_cont=container1,
            dst_pool=self.pool, dst_cont=container1,
            test_desc="copy_subsets (3)")
        self.read_verify_daos(self.pool, container1, dst_path)

    def write_daos(self, pool, container, test_dir):
        """Uses mdtest to write the test file to a DAOS container."""
        self.mdtest_cmd.flags.update(self.flags_write)
        self.mdtest_cmd.test_dir.update(test_dir)
        self.mdtest_cmd.set_daos_params(self.server_group, pool, container.uuid)
        out = self.run_mdtest(self.get_mdtest_job_manager_command(self.manager), 
                              self.processes)

    def read_verify_daos(self, pool, container, test_dir):
        """Uses mdtest to read-verify the test file in a DAOS container."""
        self.mdtest_cmd.flags.update(self.flags_read)
        self.mdtest_cmd.test_dir.update(test_dir)
        self.mdtest_cmd.set_daos_params(self.server_group, pool, container.uuid)
        out = self.run_mdtest(self.get_mdtest_job_manager_command(self.manager),
                              self.processes)
    
    def run_dcp(self, src, dst,
                prefix=None,
                src_pool=None, dst_pool=None, src_cont=None, dst_cont=None,
                test_desc=None):
        """Use mpirun to execute the dcp utility"""
        # param for dcp processes
        processes = self.params.get("processes", "/run/datamover/*")

        # Set up the dcp command
        dcp = DataMover(self.hostlist_clients)
        dcp.get_params(self)
        dcp.daos_prefix.update(prefix)
        dcp.src_path.update(src)
        dcp.dest_path.update(dst)
        dcp.set_datamover_params(src_pool, dst_pool, src_cont, dst_cont)

        # Run the dcp command
        if test_desc is not None:
             self.log.info("Running dcp: {}".format(test_desc))
        try:
            dcp.run(self.workdir, processes)
        except CommandFailure as error:
            self.log.error("DCP command failed: %s", str(error))
            self.fail("Test was expected to pass but it failed: {}\n".format(test_desc))
