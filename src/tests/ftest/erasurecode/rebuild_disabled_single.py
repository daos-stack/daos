#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time
from ec_utils import ErasureCodeSingle

class EcodDisabledRebuildSingle(ErasureCodeSingle):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object for single data
            type, after killing servers when pool rebuild is off.
    :avocado: recursive
    """
    def test_ec_degrade_single_value(self):
        """Jira ID: DAOS-7314.

        Test Description: Test Erasure code object for single type.
        Use Case: Create the pool, disabled rebuild, Write single data
                  type with EC object classes. kill single server,
                  read data and verified, kill another server,
                  read data with parity 2 and verify the content.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_single,ec_disabled_rebuild,rebuild
        :avocado: tags=ec_disabled_rebuild_single

        """
        # Disabled pool Rebuild
        self.pool.set_property("self_heal", "exclude")

        # Write single type data set with all the EC object type
        self.write_single_type_dataset()

        # Read data set with given all the EC object type
        self.read_single_type_dataset()

        # Kill the last server rank and wait for 20 seconds,
        # Rebuild is disabled so data will not be rebuild.
        self.server_managers[0].stop_ranks(
            [self.server_count - 1], self.d_log, force=True)
        time.sleep(20)

        # Read data set and verify for different EC object for parity 1 and 2.
        self.read_single_type_dataset()

        # Kill another server rank and wait for 20 seconds,
        # Rebuild is disabled so data will not be rebuild.
        self.server_managers[0].stop_ranks(
            [self.server_count - 2], self.d_log, force=True)
        time.sleep(20)

        # Read data set and verify for different EC object for 2 only.
        self.read_single_type_dataset(parity=2)
