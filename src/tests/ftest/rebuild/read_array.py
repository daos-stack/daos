#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

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
from apricot import skipForTicket
from rebuild_test_base import RebuildTestBase


class ReadArrayTest(RebuildTestBase):
    # pylint: disable=too-many-ancestors
    """Run rebuild tests with DAOS servers and clients.

    :avocado: recursive
    """

    CANCEL_FOR_TICKET = [["DAOS-2799", "targets", 8]]

    def execute_during_rebuild(self):
        """Read the objects during rebuild."""
        message = "Reading the array objects during rebuild"
        self.log.info(message)
        self.d_log.info(message)
        self.assertTrue(
            self.pool.read_data_during_rebuild(self.container),
            "Error reading data during rebuild")

    @skipForTicket("DAOS-6450")
    def test_read_array_during_rebuild(self):
        """Jira ID: DAOS-691.

        Test Description:
            Configure 5 targets with 1 pool with a service leader quantity
            of 2.  Add 1 container to the pool configured with 3 replicas.
            Add 10 objects of 10 records each populated with an array of 5
            values (currently a sufficient amount of data to be read fully
            before rebuild completes) to a specific rank.  Exclude this
            rank and verify that rebuild is initiated.  While rebuild is
            active, confirm that all the objects and records can be read.
            Finally verify that rebuild completes and the pool info indicates
            the correct number of rebuilt objects and records.

        Use Cases:
            Basic rebuild of container objects of array values with sufficient
            numbers of rebuild targets and no available rebuild targets.

        :avocado: tags=all,medium,full_regression,rebuild,rebuildreadarray
        """
        self.execute_rebuild_test()
