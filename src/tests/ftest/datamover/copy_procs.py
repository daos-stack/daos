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
from ior_test_base import IorTestBase
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from data_mover_utils import DataMover
import os
import uuid

    
class CopyProcsTest(IorTestBase):
    """
    Test Class Description:
        Tests multi-process (rank) copying of the datamover utility.
        Tests the following cases:
            Copying with varying numbers of processes (ranks).
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
       	"""Initialize a CopyBasicsTest object."""
        super(CopyProcsTest, self).__init__(*args, **kwargs)

        # Track pools and containers for proper tearDown
        self.pool = None
        self.container = []

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(CopyProcsTest, self).setUp()
        
        # Get the parameters
        self.flags_write = self.params.get("flags_write", "/run/ior/copy_procs/*")
        self.flags_read = self.params.get("flags_read", "/run/ior/copy_procs/*")

    def tearDown(self):
        """Tear down each test case."""
        # Stop the servers and agents
        super(CopyProcsTest, self).tearDown()
    
    def create_pool(self):
        """Create a TestPool object."""
        # Get the pool params
        pool = TestPool(
            self.context, dmg_command=self.get_dmg_command())
        pool.get_params(self)

        # Create a pool
        pool.create()
        
        # Save pool
        self.pool = pool

        return pool

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

        # Save container
        self.container.append(container)

        return container

    def test_copy_procs(self):
        """
        Test Description:
            DAOS-TODO: Verify multi-process (rank) copying.
        Use Cases:
            Create pool.
            Crate POSIX container1 and container2 in pool.
            Create a single 100M file in container1 using ior.

        :avocado: tags=all,pr,datamover
        :avocado: tags=copy_procs
        """
        # Create pool and containers
        pool1 = self.create_pool()
        container1 = self.create_cont(pool1)
        container2 = self.create_cont(pool1)

        # Get the varying number of processes
        procs_list = self.params.get("processes", "/run/datamover/copy_procs/*")

        # Create the test file
        self.write_daos(pool1, container1)
        
        # Run with varying number of processes
        for num_procs in procs_list:
            test_desc="copy_procs (processes={})".format(num_procs)
            self.run_dcp(
                src="/", dst ="/",
                processes=num_procs,
                src_pool=pool1, src_cont=container1,
                dst_pool=pool1, dst_cont=container2,
                test_desc=test_desc)
            self.read_verify_daos(pool1, container2)

    def write_daos(self, pool, container):
        """Uses ior to write the test file to a DAOS container."""
        self.ior_cmd.flags.update(self.flags_write)
        self.ior_cmd.set_daos_params(self.server_group, pool, container.uuid)
        out = self.run_ior(self.get_ior_job_manager_command(), self.processes)

    def read_verify_daos(self, pool, container):
        """Uses ior to read-verify the test file in a DAOS container."""
        self.ior_cmd.flags.update(self.flags_read)
        self.ior_cmd.set_daos_params(self.server_group, pool, container.uuid)
        out = self.run_ior(self.get_ior_job_manager_command(), self.processes)

    def run_dcp(self, src, dst,
                processes,
                src_pool=None, dst_pool=None, src_cont=None, dst_cont=None,
                test_desc=None):
        """Use mpirun to execute the dcp utility"""
        # Set up the dcp command
        dcp = DataMover(self.hostlist_clients)
        dcp.get_params(self)
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
