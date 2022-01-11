#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class HarnessSetupTest(TestWithServers):
    """Harness setup test cases.

    :avocado: recursive
    """

    def test_setup(self):
        """Verify the TestWithServers.setUp() method.

        Useful for setting up the /etc/daos/daos_server.yml files on multiple hosts.

        :avocado: tags=all
        :avocado: tags=hw,large
        :avocado: tags=harness,harness_setup_test,test_setup
        """
        self.log.info("Test passed!")
