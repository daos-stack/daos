'''
  (C) Copyright 2018-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import json
import re

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from general_utils import append_error, report_errors
from run_utils import run_remote
from server_utils_base import DaosServerCommandRunner


class DAOSVersion(TestWithServers):
    """JIRA ID: DAOS-8539

    Verify version number and the output string.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DAOSVersion object."""
        super().__init__(*args, **kwargs)

        # Don't waste time starting servers and agents.
        self.setup_start_servers = False
        self.setup_start_agents = False

    def test_version(self):
        """Verify version number for dmg, daos, daos_server, and daos_agent against RPM.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=control,daos_cmd
        :avocado: tags=DAOSVersion,test_version
        """
        # Get RPM version.
        rpm_command = "rpm -qa | grep daos-server"
        result = run_remote(self.log, self.hostlist_servers, rpm_command)
        if not result.passed:
            self.fail("Failed to list daos-server RPMs")
        if not result.homogeneous:
            self.fail("Non-homogenous daos-server RPMs")
        match = re.findall(r"daos-server-[tests-|tests_openmpi-]*([\d.]+)", result.joined_stdout)
        if not match:
            self.fail("Failed to get version from daos-server RPMs")
        rpm_version = match[0]
        self.log.info("RPM version = %s", rpm_version)

        # Get dmg version.
        dmg_version = self.get_dmg_command().version()["response"]["version"]
        self.log.info("dmg version = %s", dmg_version)

        # Get daos version.
        daos_version = self.get_daos_command().version()["response"]["version"]
        self.log.info("daos version = %s", daos_version)

        errors = []

        # Get daos_agent version.
        daos_agent_version = None
        daos_agent_cmd = "daos_agent --json version"
        result = run_remote(self.log, NodeSet(self.hostlist_servers[0]), daos_agent_cmd)
        if not result.passed:
            self.fail("Failed to get daos_agent version")
        daos_agent_version = json.loads(result.joined_stdout)["response"]["version"]
        self.log.info("daos_agent version = %s", daos_agent_version)

        # Get daos_server version
        daos_server_cmd = DaosServerCommandRunner(path=self.bin)
        daos_server_version = daos_server_cmd.version()["response"]["version"]
        self.log.info("daos_server version = %s", daos_server_version)

        # Verify the tool versions against the RPM.
        tool_versions = [
            ("dmg", dmg_version),
            ("daos", daos_version),
            ("daos_agent", daos_agent_version),
            ("daos_server", daos_server_version)
        ]
        for tool_version in tool_versions:
            tool = tool_version[0]
            version = tool_version[1]
            if version != rpm_version:
                msg = "Unexpected version! {} = {}, RPM = {}".format(
                    tool, version, rpm_version)
                append_error(errors, msg)

        report_errors(self, errors)
        self.log.info("Test passed")
