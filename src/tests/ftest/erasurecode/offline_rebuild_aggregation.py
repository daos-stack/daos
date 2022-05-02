#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time
from ec_utils import ErasureCodeIor, check_aggregation_status
from apricot import skipForTicket

class EcodAggregationOffRebuild(ErasureCodeIor):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object for offline rebuild
                        with different Aggregation mode for Partial strip data.

    :avocado: recursive
    """
    def execution(self, agg_trigger=False):
        """
        Common method to run the tests.

        Args:
           agg_trigger(Boolean): Check if Aggregation is happening after IO
                                 or not, By default it's False.

        """
        self.pool.connect()
        self.log.info("pool_percentage Before = %s",
                      self.pool.pool_percentage_used())

        # Write the IOR data set with given all the EC object type
        self.ior_write_dataset()

        if agg_trigger:
            # Verify that Aggregation is getting started
            if not any(check_aggregation_status(self.pool).values()):
                self.fail("Aggregation failed to start..")
        else:
            # Verify that Aggregation is not starting
            if any(check_aggregation_status(self.pool).values()) is True:
                self.fail("Aggregation should not happens...")

        # Read IOR data and verify content
        self.ior_read_dataset()

        # Kill the last server rank
        self.server_managers[0].stop_ranks([self.server_count - 1], self.d_log,
                                           force=True)

        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        # Read IOR data and verify for different EC object data still OK
        # written before killing the single server
        self.ior_read_dataset()

        # Kill the another server rank
        self.server_managers[0].stop_ranks([self.server_count - 2], self.d_log,
                                           force=True)

        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        # Read IOR data and verify for different EC object and different sizes
        # written before killing the second server.
        # Only +2 (Parity) data will be intact so read and verify only +2 IOR
        # data set
        self.ior_read_dataset(parity=2)

    def test_ec_offline_rebuild_agg_disabled(self):
        """Jira ID: DAOS-7313.

        Test Description: Test Erasure code object aggregation disabled mode
                          with IOR.
        Use Case: Create the pool, disabled aggregation, run IOR with supported
                  EC object type with partial strip.
                  Verify that Aggregation should not triggered.
                  Verify the IOR read data at the end.
                  Kill single server and wait for rebuild.
                  Read and verify all the data.
                  Kill second server and wait for rebuild.
                  Read and verify data with +2 Parity with no data corruption.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,aggregation,ec_array,ec_aggregation
        :avocado: tags=ec_offline_rebuild_agg_disabled
        """
        # Disable the aggregation
        self.pool.set_property("reclaim", "disabled")
        self.execution()

    @skipForTicket("DAOS-8542")
    def test_ec_offline_rebuild_agg_default(self):
        """Jira ID: DAOS-7313.

        Test Description: Test Erasure code object aggregation enabled(default)
                          mode with IOR.
        Use Case: Create the pool,
                  run IOR with supported EC object type with partial strip.
                  Verify the Aggregation gets triggered and space is getting
                  reclaimed.
                  Verify the IOR read data at the end.
                  Kill single server and wait for rebuild.
                  Read and verify all the data.
                  Kill second server and wait for rebuild.
                  Read and verify data with +2 Parity with no data corruption.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,aggregation,ec_array,ec_aggregation
        :avocado: tags=ec_offline_rebuild_agg_default
        """
        self.execution(agg_trigger=True)

    @skipForTicket("DAOS-8542")
    def test_ec_offline_agg_during_rebuild(self):
        """Jira ID: DAOS-7313.

        Test Description: Test Erasure code object aggregation time mode with
                          IOR.
        Use Case: Create the pool, disabled aggregation.
                  run IOR with supported EC object type with partial strip.
                  Enable Aggregation as time mode. Wait for 20 seconds where
                  it will trigger the aggregation
                  Kill single server after 20 seconds and wait for rebuild.
                  Read and verify all the data.
                  Kill second server and wait for rebuild.
                  Read and verify data with +2 Parity with no data corruption.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,aggregation,ec_array,ec_aggregation
        :avocado: tags=ec_offline_agg_during_rebuild
        """
        # Disable the aggregation
        self.pool.set_property("reclaim", "disabled")
        self.pool.connect()

        # Write the IOR data set with given all the EC object type
        self.ior_write_dataset()

        # Read IOR data and verify content
        self.ior_read_dataset()

        # Set time mode aggregation
        self.pool.set_property("reclaim", "time")

        # Aggregation will start in 20 seconds after it sets to time mode.
        # So wait for 20 seconds and kill the last server rank
        time.sleep(20)
        self.server_managers[0].stop_ranks([self.server_count - 1], self.d_log,
                                           force=True)

        # Verify if Aggregation is getting started
        if not any(check_aggregation_status(self.pool).values()):
            self.fail("Aggregation failed to start..")

        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        # Read IOR data and verify for different EC object data still OK
        # written before killing the single server
        self.ior_read_dataset()

        # Kill the another server rank
        self.server_managers[0].stop_ranks([self.server_count - 2], self.d_log,
                                           force=True)

        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        # Read IOR data and verify for different EC object and different sizes
        # written before killing the second server.
        # Only +2 (Parity) data will be intact so read and verify only +2 IOR
        # data set
        self.ior_read_dataset(parity=2)
