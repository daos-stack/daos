#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from command_utils_base import CommandFailure


class ContainerAutotestTest(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Tests container autotest.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize object."""
        super().__init__(*args, **kwargs)
        self.daos_cmd = None

    def test_container_autotest(self):
        """Test container autotest.

        :avocado: tags=all,full_regression,daily,hw,small,quick,autotest
        """
        # Create a pool
        self.log.info("Create a pool")
        self.add_pool()
        self.daos_cmd = self.get_daos_command()
        self.log.info("Autotest start")
        try:
            self.daos_cmd.pool_autotest(pool=self.pool.uuid)
            self.log.info("daos pool autotest passed.")
        except CommandFailure as error:
            self.log.error("Error: %s", error)
            self.fail("daos pool autotest failed!")
