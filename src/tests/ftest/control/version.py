'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import re
import json

from apricot import TestWithServers, skipForTicket
from general_utils import run_pcmd, report_errors
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

    @skipForTicket("DAOS-13380")
    def test_version(self):
        """Verify version number for dmg, daos, daos_server, and daos_agent against RPM.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=control,daos_cmd
        :avocado: tags=version_number,test_version
        """
        errors = []

        # Get RPM version.
        rpm_command = "rpm -qa|grep daos-server"
        output = run_pcmd(hosts=self.hostlist_servers, command=rpm_command)
        self.log.info("RPM output = %s", output)
        stdout = output[0]["stdout"][0]
        self.log.info("RPM stdout = %s", stdout)
        result = re.findall(r"daos-server-[tests-|tests_openmpi-]*([\d.]+)", stdout)
        if not result:
            errors.append("RPM version is not in the output! {}".format(output))
        else:
            rpm_version = result[0]
            self.log.info("RPM version = %s", rpm_version)

        # Get dmg version.
        dmg_version = self.get_dmg_command().version()["response"]["version"]
        self.log.info("dmg version = %s", dmg_version)

        # Get daos version.
        daos_version = self.get_daos_command().version()["response"]["version"]
        self.log.info("daos version = %s", daos_version)

        # Get daos_agent version.
        daos_agent_cmd = "daos_agent --json version"
        output = run_pcmd(hosts=self.hostlist_servers, command=daos_agent_cmd)
        daos_agent_version = json.loads("".join(output[0]["stdout"]))["response"]["version"]
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
                errors.append(msg)

        self.log.info("###### Test Result ######")
        report_errors(test=self, errors=errors)
        self.log.info("#########################")
