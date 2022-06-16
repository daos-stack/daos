#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from ec_utils import ErasureCodeIor, check_aggregation_status

class EcodServerRestart(ErasureCodeIor):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object data after restarting all servers.
    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.percent = self.params.get("size", '/run/ior/data_percentage/*')

    def execution(self, agg_check=None):
        """
        Common test execution method to write data, verify aggregation, restart
        all the servers and read data.

        Args:
            agg_check: When to check Aggregation status.Either before restarting all the servers or
                       after.Default is None so not to check aggregation, but wait for 20 seconds
                       and restart the servers.
        """
        # Write all EC object data to NVMe
        self.ior_write_dataset(operation="Auto_Write", percent=self.percent)
        self.log.info(self.pool.pool_percentage_used())
        # Write all EC object data to SCM
        self.ior_write_dataset(storage='SCM', operation="Auto_Write", percent=self.percent)
        self.log.info(self.pool.pool_percentage_used())
        size_before_restart = self.pool.pool_percentage_used()

        if not agg_check:
            # Set time mode aggregation
            self.pool.set_property("reclaim", "time")
            # Aggregation will start in 20 seconds after it sets to time mode. So wait for 20
            # seconds and restart all the servers.
            time.sleep(20)

        if agg_check == "Before":
            # Verify if Aggregation is getting started
            if not any(check_aggregation_status(self.pool, attempt=50).values()):
                self.fail("Aggregation failed to start Before server restart..")

        # Shutdown the servers and restart
        self.get_dmg_command().system_stop(True)
        time.sleep(5)
        self.get_dmg_command().system_start()

        if agg_check == "After":
            size_after_restart = self.pool.pool_percentage_used()
            self.log.info("Size after Restarti: %s ", self.pool.pool_percentage_used())
            # Verify if Aggregation is getting started
            if not any(check_aggregation_status(self.pool, attempt=50).values()):
                self.fail("Aggregation failed to start After server restart..")

        # Read all EC object data from NVMe
        self.ior_read_dataset(operation="Auto_Read", percent=self.percent)
        # Read all EC object data which was written on SCM
        self.read_set_from_beginning = False
        self.ior_read_dataset(storage='SCM', operation="Auto_Read", percent=self.percent)

    def test_ec_restart_before_agg(self):
        """Jira ID: DAOS-7337.

        Test Description: Test Erasure code object with IOR after all server restart and Aggregation
                            trigger before restart.
        Use Case: Create the pool, run IOR with supported EC object type class for small and
                    large transfer sizes.Verify aggregation starts, Restart all the servers.
                    Read and verify all IOR data.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_array,ec_server_restart,ec_aggregation
        :avocado: tags=ec_restart_before_agg
        """
        self.execution(agg_check="Before")

    def test_ec_restart_after_agg(self):
        """Jira ID: DAOS-7337.

        Test Description: Test Erasure code object with IOR after all server restart and Aggregation
                            trigger after restart.
        Use Case: Create the pool, run IOR with supported EC object type class for small and
                large transfer sizes.Restart all servers. Verify Aggregation trigger after it's
                start. Read and verify all IOR data.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_array,ec_server_restart,ec_aggregation
        :avocado: tags=ec_restart_after_agg
        """
        self.execution(agg_check="After")

    def test_ec_restart_during_agg(self):
        """Jira ID: DAOS-7337.

        Test Description: Test server restart works during aggregation and IOR data is intact
                            after restart.
        Use Case: Create the pool, disabled rebuild, run IOR with supported EC object type class
                    for small and large transfer sizes. Enable the time mode aggregation and wait
                    for 20 seconds. Restart all servers, Read and verify all IOR data.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_array,ec_server_restart,ec_aggregation
        :avocado: tags=ec_restart_during_agg
        """
        # Disable the aggregation
        self.pool.set_property("reclaim", "disabled")
        self.execution()
