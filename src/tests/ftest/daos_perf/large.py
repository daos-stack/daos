#!/usr/bin/python3
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from daos_perf_base import DaosPerfBase


class DaosPerfLarge(DaosPerfBase):
    # pylint: disable=too-many-ancestors
    """Tests daos_perf with different config.

    :avocado: recursive
    """
    def test_large(self):
        """Jira ID: DAOS-1714.

        Test Description:
          Large daos_perf test for performance purpose.

        Use Case:
          Run daos_perf for scm and nvme.
          Run daos_perf with 'EC2P1' object class.
          Run the combination of above test cases with large number of clients
          on four servers.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daosperf,daosperflarge
        """
        self.run_daos_perf()
