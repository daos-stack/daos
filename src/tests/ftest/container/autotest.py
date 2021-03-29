#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from test_utils_container import TestContainer


class ContainerAutotestTest(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Tests container autotest.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize object."""
        super().__init__(*args, **kwargs)
        self.pool = None
        self.container = None

    def test_container_autotest(self):
        """Test container autotest.

        :avocado: tags=all,full_regression,daily,hw,quick,autotest
        """
        # Create a pool
        self.log.info("Create a pool")
        self.add_pool()
        self.daos_cmd = self.get_daos_command()
        self.log.info("Autotest start")
        self.daos_cmd.pool_autotest(pool=self.pool.uuid)
