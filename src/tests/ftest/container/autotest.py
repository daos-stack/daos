#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from exception_utils import CommandFailure


class ContainerAutotestTest(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Tests container autotest.

    :avocado: recursive
    """

    def test_container_autotest(self):
        """Test container autotest.

        :avocado: tags=all,full_regression,daily_regression
        :avocado: tags=hw,small
        :avocado: tags=container,autotest,containerautotest,quick
        """
        self.log.info("Create a pool")
        self.add_pool()
        self.pool.set_query_data()
        daos_cmd = self.get_daos_command()
        self.log.info("Autotest start")
        try:
            daos_cmd.pool_autotest(pool=self.pool.uuid)
            self.log.info("daos pool autotest passed.")
        except CommandFailure as error:
            self.log.error("Error: %s", error)
            self.fail("daos pool autotest failed!")
        finally:
            self.pool.set_query_data()
