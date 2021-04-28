#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from daos_perf_base import DaosPerfBase
from apricot import skipForTicket


class DaosPerf(DaosPerfBase):
    # pylint: disable=too-many-ancestors
    """Test cases for the daos_perf command.

    Test Class Description:
        Tests daos_perf with different configurations.

    :avocado: recursive
    """

    @skipForTicket("DAOS-7256")
    def test_small(self):
        """Jira ID: DAOS-1714.

        Test Description:
            Small daos_perf test

        Use cases:
            Run daos_perf in 'daos' and 'vos' modes.  Run daos_perf using single
            value and array value types for 'vos' mode. Also run the above
            config with and without shuffle option '-S' of daos_perf.  Run
            daos_perf using single value type for 'LARGE' and 'R2s' object
            class. Run this config with multiple server/client configuration.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daosperf,daosperfsmall
        """
        self.run_daos_perf()
