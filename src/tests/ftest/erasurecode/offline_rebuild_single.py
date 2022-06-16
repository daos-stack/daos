#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ec_utils import ErasureCodeSingle

class EcodOfflineRebuildSingle(ErasureCodeSingle):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object single type data
                            after killing servers (offline rebuild).
    :avocado: recursive
    """

    def test_ec_offline_rebuild_single(self):
        """Jira ID: DAOS-7314.

        Test Description: Test Erasure code object for single type.
        Use Case: Create the pool, Write single data type with EC object
                  classes. kill single server, wait for rebuild to finish,
                  read data and verified, kill another server,
                  wait for rebuild to finish,
                  read data with parity 2 and verify the content.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_single,ec_offline_rebuild,rebuild
        :avocado: tags=ec_offline_rebuild_single

        """
        # Write single type data set with all the EC object type
        self.write_single_type_dataset()

        # Kill the last server rank
        self.server_managers[0].stop_ranks([self.server_count - 1], self.d_log,
                                           force=True)

        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        # Read data set and verify for different EC object for parity 1 and 2.
        self.read_single_type_dataset()

        # Kill the another server rank
        self.server_managers[0].stop_ranks([self.server_count - 2], self.d_log,
                                           force=True)

        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        # Read data set and verify for different EC object for 2 only.
        self.read_single_type_dataset(parity=2)
