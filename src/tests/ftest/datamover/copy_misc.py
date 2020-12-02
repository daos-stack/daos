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


class CopyMiscTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for misc POSIX DataMover validation.

    Test Class Description:
        Tests misc functionality of the POSIX DataMover utilities.
        Tests the following cases:
            Auto-creating destination POSIX containers on copy.
    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(CopyMiscTest, self).setUp()

        # Get the parameters
        self.ior_flags = self.params.get(
            "ior_flags", "/run/ior/*")
        self.test_file = self.ior_cmd.test_file.value

    def test_copy_auto_create_dest_dcp(self):
        """
        Test Description:
            Tests auto-creation of destination POSIX container.
            Uses the dcp tool.
            DAOS-5653: Verify auto-creation of destination container
        Use Cases:
            Create pool1 and pool2.
            Create POSIX cont1 in pool1.
            Create a single 1K file in cont1 using ior.
            Copy all data from cont1 to pool1, with a new cont UUID.
            Copy all data from cont1 to pool2, with a new cont UUID.
        :avocado: tags=all,daily_regression
        :avocado: tags=datamover
        :avocado: tags=copy_misc,copy_auto_create_dest_dcp
        """
        self.set_tool("DCP")

        # Create pools and src container
        pool1 = self.create_pool()
        pool2 = self.create_pool()
        container1 = self.create_cont(pool1)

        # Create the source file
        self.run_ior_with_params("DAOS", "/", pool1, container1,
                                 self.test_file, self.ior_flags[0])

        # pool1 -> pool1
        new_uuid = self.gen_uuid()
        self.run_datamover(
            "copy_auto_create_dest (same pool)",
            "DAOS", "/", pool1, container1,
            "DAOS", "/", pool1, new_uuid)
        self.run_ior_with_params("DAOS", "/", pool1, new_uuid,
                                 self.test_file, self.ior_flags[1])

        # pool1 -> pool2
        new_uuid = self.gen_uuid()
        self.run_datamover(
            "copy_auto_create_dest (different pool)",
            "DAOS", "/", pool1, container1,
            "DAOS", "/", pool2, new_uuid)
        self.run_ior_with_params("DAOS", "/", pool2, new_uuid,
                                 self.test_file, self.ior_flags[1])
