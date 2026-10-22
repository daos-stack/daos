"""
(C) Copyright 2021-2023 Intel Corporation.
(C) Copyright 2026 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class TestServerTimeouts(TestWithServers):
    """Test class for verifying server timeouts.

    :avocado: recursive
    """

    def _verify_server_timeouts(self):
        """Verify the server prepare and format timeout values."""
        for entry in ("storage_prepare_timeout", "storage_format_timeout"):
            self.log_step(f"Verifying server {entry}")
            value = self.params.get(entry)
            if getattr(self.server_managers[0], entry).value != value:
                self.fail(f"Server {entry} was not set correctly from the test yaml")
        self.log_step("Test passed!")


class HarnessSetupTest(TestServerTimeouts):
    """Harness setup test cases.

    Also useful for setting up the /etc/daos/daos_server.yml files on multiple hosts.

    :avocado: recursive
    """

    def test_setup_hw(self):
        """Verify the TestWithServers.setUp() method.

        :avocado: tags=all
        :avocado: tags=hw,medium,large
        :avocado: tags=harness,server_setup
        :avocado: tags=HarnessSetupTest,test_setup_hw
        """
        self._verify_server_timeouts()

    def test_setup_hw_provider(self):
        """Verify the TestWithServers.setUp() method.

        :avocado: tags=all
        :avocado: tags=hw,medium,large,provider
        :avocado: tags=harness,server_setup
        :avocado: tags=HarnessSetupTest,test_setup_hw_provider
        """
        self._verify_server_timeouts()

    def test_setup_hw_vmd(self):
        """Verify the TestWithServers.setUp() method.

        :avocado: tags=all
        :avocado: tags=hw_vmd,medium,large
        :avocado: tags=harness,server_setup
        :avocado: tags=HarnessSetupTest,test_setup_hw_vmd
        """
        self._verify_server_timeouts()

    def test_setup_cb(self):
        """Verify the TestWithServers.setUp() method.

        :avocado: tags=all
        :avocado: tags=cb,medium,large
        :avocado: tags=harness,server_setup
        :avocado: tags=HarnessSetupTest,test_setup_cb
        """
        self._verify_server_timeouts()

    def test_setup_cb_provider(self):
        """Verify the TestWithServers.setUp() method.

        :avocado: tags=all
        :avocado: tags=cb,medium,large,provider
        :avocado: tags=harness,server_setup
        :avocado: tags=HarnessSetupTest,test_setup_cb_provider
        """
        self._verify_server_timeouts()
