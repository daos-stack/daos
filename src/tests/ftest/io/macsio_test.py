#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from general_utils import convert_list
from dfuse_test_base import DfuseTestBase
from macsio_test_base import MacsioTestBase


class MacsioTest(DfuseTestBase, MacsioTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs a basic MACSio test.

    :avocado: recursive
    """

    # Cancel any test using MPICH w/ MACSio due to DAOS-5265
    CANCEL_FOR_TICKET = [["DAOS-5265", "job_manager_mpi_type", "mpich"]]

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
