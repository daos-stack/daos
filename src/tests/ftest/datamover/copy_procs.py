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
from data_mover_test_base import DataMoverTestBase
from os.path import join, sep


class CopyProcsTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for Datamover multiple processes.

    Test Class Description:
        Tests multi-process (rank) copying of the datamover utility.
        Tests the following cases:
            Copying with varying numbers of processes (ranks).

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(CopyProcsTest, self).setUp()

        # Get the parameters
        self.test_file = self.params.get(
            "test_file", "/run/ior/*")
        self.flags_write = self.params.get(
            "flags_write", "/run/ior/copy_procs/*")
        self.flags_read = self.params.get(
            "flags_read", "/run/ior/copy_procs/*")

        # Setup the directory structures
        self.posix_test_paths.append(join(self.workdir, "posix_test") + sep)
        self.posix_test_paths.append(join(self.workdir, "posix_test2") + sep)
        self.posix_test_file = join(self.posix_test_paths[0], self.test_file)
        self.posix_test_file2 = join(self.posix_test_paths[1], self.test_file)
        self.daos_test_file = join("/", self.test_file)

        # Create the directories
        cmd = "mkdir -p {}".format(self.get_posix_test_path_string())
        self.execute_cmd(cmd)

    def test_copy_procs(self):
        """
        Test Description:
            DAOS-5659: Verify multi-process (rank) copying.
        Use Cases:
            Create pool.
            Crate POSIX container1 and container2 in pool.
            Create a single 100M file in container1 using ior.
        :avocado: tags=all,pr,hw,small,datamover
        :avocado: tags=copy_procs
        """
        # Create pool and containers
        pool1 = self.create_pool()
        container1 = self.create_cont(pool1)
        container2 = self.create_cont(pool1)

        # Get the varying number of processes
        procs_list = self.params.get(
            "processes", "/run/datamover/copy_procs/*")

        # Create the test files
        self.set_ior_location_and_run("DAOS_UUID", self.daos_test_file,
                                      pool1, container1,
                                      flags=self.flags_write)
        self.set_ior_location_and_run("POSIX", self.posix_test_file,
                                      flags=self.flags_write)

        # DAOS -> POSIX
        # Run with varying number of processes
        self.set_src_location("DAOS_UUID", "/", pool1, container1)
        self.set_dst_location("POSIX", self.posix_test_paths[1])
        for num_procs in procs_list:
            test_desc = "copy_procs (DAOS->POSIX with {} procs)".format(
                num_procs)
            self.run_datamover(
                test_desc=test_desc,
                processes=num_procs)
            self.set_ior_location_and_run("POSIX", self.posix_test_file2,
                                          flags=self.flags_read)

        # POSIX -> DAOS
        # Run with varying number of processes
        self.set_src_location("POSIX", self.posix_test_paths[0])
        self.set_dst_location("DAOS_UUID", "/", pool1, container2)
        for num_procs in procs_list:
            test_desc = "copy_procs (POSIX->DAOS with {} processes)".format(
                num_procs)
            self.run_datamover(
                test_desc=test_desc,
                processes=num_procs)
            self.set_ior_location_and_run("DAOS_UUID", self.daos_test_file,
                                          pool1, container2,
                                          flags=self.flags_read)
