'''
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time
from ec_utils import ErasureCodeIor


class EcodDisabledRebuild(ErasureCodeIor):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object data after killing
                            single server when pool rebuild is off.
    :avocado: recursive
    """

    def test_ec_degrade(self):
        """Jira ID: DAOS-5893.

        Test Description: Test Erasure code object with IOR.
        Use Case: Create the pool, disabled rebuild, run IOR with supported
                  EC object type class for small and large transfer sizes.
                  kill single server, verify all IOR read data and verified.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array,ec_disabled_rebuild,rebuild
        :avocado: tags=EcodDisabledRebuild,test_ec_degrade
        """
        # Disabled pool Rebuild
        self.pool.set_property("self_heal", "exclude")
        # self.pool.set_property("reclaim", "disabled")

        # Write the IOR data set with given all the EC object type
        self.ior_write_dataset()

        # Verify if Aggregation is getting started
        if not any(self.check_aggregation_status(attempt=60).values()):
            self.fail("Aggregation failed to start..")

        # Kill the last server rank and wait for 20 seconds, Rebuild is disabled
        # so data should not be rebuild
        self.server_managers[0].stop_ranks([self.server_count - 1], self.d_log,
                                           force=True)
        time.sleep(20)

        # Read IOR data and verify for different EC object and different sizes
        # written before killing the single server
        self.ior_read_dataset()

        # Kill the another server rank and wait for 20 seconds,Rebuild will
        # not happens because i's disabled.Read/verify data with Parity 2.
        self.server_managers[0].stop_ranks([self.server_count - 2], self.d_log,
                                           force=True)
        time.sleep(20)

        # Read IOR data and verify for different EC object and different sizes
        # written before killing the single server
        self.ior_read_dataset(parity=2)
