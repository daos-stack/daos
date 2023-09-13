"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers

from run_utils import run_remote


class VerifyPoolSpace(TestWithServers):
    """Verify pool space with system commands.

    :avocado: recursive"""

    def test_verify_pool_space(self):
        """Test ID: DAOS-3672.

        Test steps:
        1) Create a single pool on a single server and list associated storage, verify correctness
        2) Use IOR to fill containers to varying degrees of fullness, verify storage listing
        3) Create multiple pools on a single server, list associated storage, verify correctness
        4) Use IOR to fill containers to varying degrees of fullness, verify storage listing for all
           pools
        5) Create a single pool that spans many servers, list associated storage, verify correctness
        6) Use IOR to fill containers to varying degrees of fullness, verify storage listing
        7) Create multiple pools that span many servers, list associated storage, verify correctness
        8) Use IOR to fill containers to varying degrees of fullness, verify storage listing
        9) Fail one of the servers for a pool spanning many servers.  Verify the storage listing.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=VerifyPoolSpace,test_verify_pool_space
        """
        # ranks = self.server_managers[0].ranks
        self.verify_pool_space('a single pool on a single server', [1])
        self.verify_pool_space('multiple pools on a single server', [2, 3, 4])
        self.verify_pool_space('a single pool that spans many servers', [5])
        self.verify_pool_space('multiple pools that span many servers', [6, 7])

    def verify_pool_space(self, description, indices):
        """."""
        pools = []
        self.log_step(' '.join(['Create', description]), True)
        for index in indices:
            namespace = os.path.join(os.sep, 'run', '_'.join(['pool', index]))
            pools.append(self.get_pool(namespace=namespace))

        self.log_step(' '.join(['Write data to', description]))

        self.verify_pool_size(description, pools)

    def verify_pool_size(self, description, pools):
        """."""
        self.log_step(' '.join(['Collect system-level DAOS mount information for', description]))
        command = 'df -h | grep daos'
        result = run_remote(self.log, self.server_managers[0].hosts, command, stderr=True)
        if not result.passed:
            self.fail('Error collecting system level daos mount information')

        self.log_step(' '.join(['Query pool information for', description]))
        for pool in pools:
            pool.query()
