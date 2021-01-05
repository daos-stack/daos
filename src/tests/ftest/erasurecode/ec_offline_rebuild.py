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
from ec_utils import ErasureCodeIor
from apricot import skipForTicket

class EcOfflineRebuild(ErasureCodeIor):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object data after killing
                            single server (offline rebuild).
    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Initialize a ErasureCodeIor object."""
        super(EcOfflineRebuild, self).__init__(*args, **kwargs)

    def setUp(self):
        """Set up for test case."""
        super(EcOfflineRebuild, self).setUp()

    @skipForTicket("DAOS-5377")
    def test_ec_offline_rebuild(self):
        """Jira ID: DAOS-5894.

        Test Description: Test Erasure code object with IOR.
        Use Case: Create the pool, run IOR with supported
                  EC object type class for small and large transfer sizes.
                  kill single server, Wait to finish rebuild,
                  verify all IOR read data and verified.

        :avocado: tags=all,hw,large,ib2,full_regression
        :avocado: tags=ec,ec_offline_rebuild
        """
        #Write IOR data set with different EC object and different sizes
        self.ior_write_dataset()

        # Kill the last server rank
        self.get_dmg_command().system_stop(True, self.server_count - 1)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True)
        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        #Read IOR data and verify for different EC object and different sizes
        #written before killing the single server
        self.ior_read_dataset()

        # Kill the another server rank
        self.get_dmg_command().system_stop(True, self.server_count - 2)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True)
        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        #Read IOR data and verify for different EC object and different sizes
        #written before killing the second server.
        #Only +2 (Parity) data will be intact so read and verify only +2 IOR
        #data set
        self.ior_read_dataset(parity=2)
