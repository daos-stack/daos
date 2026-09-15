"""
(C) Copyright 2021-2023 Intel Corporation.
(C) Copyright 2026 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class HarnessSetupTest(TestWithServers):
    """Harness setup test cases.

    :avocado: recursive
    """

    def __setup_test(self):
        """Run the setup test."""
        for entry in ("storage_prepare_timeout", "storage_format_timeout"):
            name = entry.replace('_', ' ')
            self.log_step(f"Verifying {name}")
            value = self.params.get(entry)
            if getattr(self.server_managers[0], entry).value != value:
                self.fail(f"{name.capitalize()} was not set correctly from the test yaml")

    def test_setup_hw(self):
        """Verify the TestWithServers.setUp() method.

        Useful for setting up the /etc/daos/daos_server.yml files on multiple hosts.

        :avocado: tags=all
        :avocado: tags=hw,medium,large
        :avocado: tags=harness,test_setup
        :avocado: tags=HarnessSetupTest,test_setup_hw
        """
        self.__setup_test()

    def test_setup_hw_provider(self):
        """Verify the TestWithServers.setUp() method.

        Useful for setting up the /etc/daos/daos_server.yml files on multiple hosts.

        :avocado: tags=all
        :avocado: tags=hw,medium,large,provider
        :avocado: tags=harness,test_setup
        :avocado: tags=HarnessSetupTest,test_setup_hw_provider
        """
        self.__setup_test()

    def test_setup_cb(self):
        """Verify the TestWithServers.setUp() method.

        Useful for setting up the /etc/daos/daos_server.yml files on multiple hosts.

        :avocado: tags=all
        :avocado: tags=cb,medium,large
        :avocado: tags=harness,test_setup
        :avocado: tags=HarnessSetupTest,test_setup_cb
        """
        self.__setup_test()

    def test_setup_cb_provider(self):
        """Verify the TestWithServers.setUp() method.

        Useful for setting up the /etc/daos/daos_server.yml files on multiple hosts.

        :avocado: tags=all
        :avocado: tags=cb,medium,large,provider
        :avocado: tags=harness,test_setup
        :avocado: tags=HarnessSetupTest,test_setup_cb_provider
        """
        self.__setup_test()
