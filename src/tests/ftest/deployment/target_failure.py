#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from ior_test_base import IorTestBase


class TargetFailure(IorTestBase):
    """Test class for pool query.

    :avocado: recursive
    """

    def test_target_failure_wo_rf(self):
        """Jira ID: DAOS-xxxx.

        :avocado: tags=all,full_regression,
        :avocado: tags=vm
        :avocado: tags=deployment
        :avocado: tags=target_failure_wo_rf
        """
        self.add_pool()

        self.log.info("Test Passed")
