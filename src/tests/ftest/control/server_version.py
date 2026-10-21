"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class ServerVersionTest(TestWithServers):
    """Test Class Description:

    Verify that server-version commands work with certificate-based authentication.

    :avocado: recursive
    """

    def _check_version_response(self, response):
        """Validate a server-version JSON response.

        Args:
            response (dict): the "response" portion of the JSON output
        """
        self.assertIn("name", response, "Missing 'name' in server-version response")
        self.assertIn("version", response, "Missing 'version' in server-version response")
        self.assertTrue(response["version"], "Empty version string")
        self.log.info("Server version: %s %s", response["name"], response["version"])

    def test_dmg_server_version(self):
        """JIRA ID: DAOS-18711.

        Test Description: Verify dmg server-version returns valid version
        information when using certificate-based authentication.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dmg,control
        :avocado: tags=ServerVersionTest,test_dmg_server_version
        """
        result = self.get_dmg_command().server_version()
        self._check_version_response(result["response"])

    def test_agent_server_version(self):
        """JIRA ID: DAOS-18711.

        Test Description: Verify daos_agent server-version returns valid version
        information when using certificate-based authentication.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=daos_agent,control
        :avocado: tags=ServerVersionTest,test_agent_server_version
        """
        result = self.agent_managers[0].server_version()
        self._check_version_response(result["response"])
