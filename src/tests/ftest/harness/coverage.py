"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from ior_utils import read_data, write_data


class HarnessCoverageTest(TestWithServers):
    """Tests for new specific metrics to track activity of md_on_ssd.

    :avocado: recursive
    """

    def test_basic_coverage(self):
        """Test basic harness coverage by starting servers, agents, dfuse, and running ior.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=harness
        :avocado: tags=HarnessCoverageTest,test_basic_coverage
        """
        self.log_step('Creating a pool (dmg pool create)')
        pool = self.get_pool(self)

        self.log_step('Creating a container for the pool (daos container create)')
        container = self.get_container(pool)

        self.log_step('Starting dfuse')
        dfuse = get_dfuse(self, self.agent_managers[0].hosts)
        start_dfuse(self, dfuse, pool, container)

        self.log_step('Writing data to the pool (ior)')
        ior = write_data(self, container, dfuse=dfuse)

        self.log_step('Reading data to the pool (ior)')
        read_data(self, ior, container, dfuse=dfuse)
