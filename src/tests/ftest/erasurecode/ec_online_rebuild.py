#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ec_utils import ErasureCodeIor
from apricot import skipForTicket

class EcOnlineRebuild(ErasureCodeIor):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object data after killing
                            single server while IOR Write in progress.
    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Initialize a ErasureCodeIor object."""
        super(EcOnlineRebuild, self).__init__(*args, **kwargs)

    def setUp(self):
        """Set up for test case."""
        super(EcOnlineRebuild, self).setUp()
        #Enabled Online rebuild
        self.set_online_rebuild = True

    @skipForTicket("DAOS-6546")
    def test_ec_offline_rebuild(self):
        """Jira ID: DAOS-5894.

        Test Description: Test Erasure code object with IOR.
        Use Case: Create the pool, run IOR with supported
                  EC object type class for small and large transfer sizes.
                  kill single server, while IOR Write phase is in progress,
                  verify all IOR write finish.Read and verify data.

        :avocado: tags=all,hw,large,ib2,full_regression
        :avocado: tags=ec,ec_online_rebuild
        """
        # Kill last server rank
        self.rank_to_kill = self.server_count - 1
        #Write IOR data set with different EC object.
        #kill single server while IOR Write phase is in progress.
        self.ior_write_dataset()

        #Disabled Online rebuild
        self.set_online_rebuild = False
        #Read IOR data and verify for different EC object
        #kill single server while IOR Read phase is in progress.
        self.ior_read_dataset()

        #Enabled Online rebuild during Read phase
        self.set_online_rebuild = True
        # Kill another server rank
        self.rank_to_kill = self.server_count - 2
        #Read IOR data and verify for EC object again
        #EC data was written with +2 parity so after killing Two servers data
        #should be intact and no data corruption observed.
        self.ior_read_dataset(parity=2)
