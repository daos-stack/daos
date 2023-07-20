"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from daos_perf_base import DaosPerfBase


class DaosPerf(DaosPerfBase):
    """Test cases for the daos_perf command.

    Test Class Description:
        Tests daos_perf with different configurations.

    :avocado: recursive
    """

    def test_small(self):
        """Jira ID: DAOS-1714.

        Test Description:
            Small daos_perf test

        Use cases:
            Run daos_perf in 'daos'.  Run daos_perf using single value type
            for 'LARGE' and 'R2s' and 'EC2P1' object class. Run this config
            with multiple server/client configuration.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=daos_perf
        :avocado: tags=DaosPerf,test_small
        """
        self.run_daos_perf()
