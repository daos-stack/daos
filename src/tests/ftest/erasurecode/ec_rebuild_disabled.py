#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time
from ec_utils import ErasureCodeIor
from apricot import skipForTicket

class EcDisabledRebuild(ErasureCodeIor):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object data after killing
                            single server when pool rebuild is off.
    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Initialize a ErasureCodeIor object."""
        super(EcDisabledRebuild, self).__init__(*args, **kwargs)

    def setUp(self):
        """Set up for test case."""
        super(EcDisabledRebuild, self).setUp()

    @skipForTicket("DAOS-6496")
    def test_ec_degrade(self):
        """Jira ID: DAOS-5893.

        Test Description: Test Erasure code object with IOR.
        Use Case: Create the pool, disabled rebuild, run IOR with supported
                  EC object type class for small and large transfer sizes.
                  kill single server, verify all IOR read data and verified.

        :avocado: tags=all,hw,large,ib2,full_regression
        :avocado: tags=ec,ec_disabled_rebuild
        """
        #Disabled pool Rebuild
        self.pool.set_property("self_heal", "exclude")
        #self.pool.set_property("reclaim", "disabled")

        #Write the IOR data set with given all the EC object type
        self.ior_write_dataset()

        # Kill the last server rank and wait for 20 seconds, Rebuild is disabled
        # so data should not be rebuild
        self.pool.start_rebuild([self.server_count - 1], self.d_log)
        time.sleep(20)

        #Read IOR data and verify for different EC object and different sizes
        #written before killing the single server
        self.ior_read_dataset()

        # Kill the another server rank and wait for 20 seconds,Rebuild will
        # not happens because i's disabled.Read/verify data with Parity 2.
        self.pool.start_rebuild([self.server_count - 2], self.d_log)
        time.sleep(20)

        #Read IOR data and verify for different EC object and different sizes
        #written before killing the single server
        self.ior_read_dataset(parity=2)
