#!/usr/bin/python3
"""
(C) Copyright 2021-2022 Intel Corporation.

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
        self.assertEqual(self.server_managers[0].storage_prepare_timeout.value, 180,
                         "FAILED: storage prepare was not set correctly from the yaml")
        self.assertEqual(self.server_managers[0].storage_format_timeout.value, 190,
                         "FAILED: storage format was not set correctly from the yaml")
        self.log.info("Test passed!")
