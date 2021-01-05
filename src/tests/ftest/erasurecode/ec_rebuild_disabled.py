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

    @skipForTicket("DAOS-5377")
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
