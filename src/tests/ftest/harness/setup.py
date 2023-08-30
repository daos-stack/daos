"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class HarnessSetupTest(TestWithServers):
    """Harness setup test cases.

    :avocado: recursive
    """

    def run_test(self):
        """Run the test."""
        if self.server_managers[0].storage_prepare_timeout.value != 60:
            self.fail("Storage prepare was not set correctly from the test yaml")
        if self.server_managers[0].storage_format_timeout.value != 60:
            self.fail("Storage format was not set correctly from the test yaml")
        self.log.info("Test passed!")

    def test_setup(self):
        """Verify the TestWithServers.setUp() method.

        Useful for setting up the /etc/daos/daos_server.yml files on multiple hosts.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness
        :avocado: tags=HarnessSetupTest,test_setup
        """
        self.run_test()

    def test_setup_hw(self):
        """Verify the TestWithServers.setUp() method.

        Useful for setting up the /etc/daos/daos_server.yml files on multiple hosts.

        :avocado: tags=all
        :avocado: tags=hw,medium,large
        :avocado: tags=harness
        :avocado: tags=HarnessSetupTest,test_setup_hw
        """
        self.run_test()
