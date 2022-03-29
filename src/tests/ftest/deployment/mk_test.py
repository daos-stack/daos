#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class MKTests(TestWithServers):
    """Test class for pool query.

    :avocado: recursive
    """

    def test_mk_deployment(self):
        """Jira ID: DAOS-xxxx.

        :avocado: tags=all,full_regression,
        :avocado: tags=vm
        :avocado: tags=deployment,test_file
        :avocado: tags=mk_test_file
        """
        self.add_pool()

        self.log.info("Test Passed")
