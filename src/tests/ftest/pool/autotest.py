#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from exception_utils import CommandFailure


class PoolAutotestTest(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Tests pool autotest.

    :avocado: recursive
    """

    def test_pool_autotest(self):
        """Test pool autotest.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=pool
        :avocado: tags=autotest,pool_autotest,quick
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
