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
from general_utils import convert_list
from dfuse_test_base import DfuseTestBase
from macsio_test_base import MacsioTestBase


class MacsioTest(DfuseTestBase, MacsioTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs a basic MACSio test.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Cancel any test using MPICH w/ MACSio due to DAOS-5265
        mpi_type = self.params.get("job_manager_mpi_type")
        if mpi_type == "mpich":
            self.cancelForTicket("DAOS-5265")
        super(MacsioTest, self).setUp()

    def test_macsio(self):
        """JIRA ID: DAOS-3658.

        Test Description:
            Purpose of this test is to check basic functionality for DAOS,
            MPICH, HDF5, and MACSio.

        Use case:
            Six clients and two servers.

        :avocado: tags=all,daily_regression,hw,large,io,macsio,DAOS_5610
        """
        # Create a pool
        self.add_pool()
        self.pool.display_pool_daos_space()

        # Create a container
        self.add_container(self.pool)

        # Run macsio
        self.log.info("Running MACSio")
        status = self.macsio.check_results(
            self.run_macsio(
                self.pool.uuid, convert_list(self.pool.svc_ranks),
                self.container.uuid),
            self.hostlist_clients)
        if status:
            self.log.info("Test passed")

    def test_macsio_daos_vol(self):
        """JIRA ID: DAOS-4983.

        Test Description:
            Purpose of this test is to check basic functionality for DAOS,
            MPICH, HDF5, and MACSio with DAOS VOL connector.

        Use case:
            Six clients and two servers.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=io,macsio_daos_vol
        :avocado: tags=DAOS_5610
        """
        plugin_path = self.params.get("plugin_path")

        # Create a pool
        self.add_pool()
        self.pool.display_pool_daos_space()

        # Create a container
        self.add_container(self.pool)

        # Create dfuse mount point
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        # VOL needs to run from a file system that supports xattr.  Currently
        # nfs does not have this attribute so it was recommended to create and
        # use a dfuse dir and run vol tests from there.
        self.job_manager.working_dir.value = self.dfuse.mount_dir.value

        # Run macsio
        self.log.info("Running MACSio with DAOS VOL connector")
        status = self.macsio.check_results(
            self.run_macsio(
                self.pool.uuid, convert_list(self.pool.svc_ranks),
                self.container.uuid, plugin_path),
            self.hostlist_clients)
        if status:
            self.log.info("Test passed")
