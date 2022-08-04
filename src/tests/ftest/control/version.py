#!/usr/bin/python3
'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import re

from apricot import TestWithServers
from general_utils import run_pcmd


class DAOSVersion(TestWithServers):
    """JIRA ID: DAOS-8539

    Verify version number and the output string.

    :avocado: recursive
    """
    def test_version(self):
        """Verify version number for dmg, daos, daos_server, and daos_agent against RPM.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=control
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
        dmg_cmd = self.get_dmg_command()
        output = dmg_cmd.version().stdout.decode("utf-8")

        # Verify that "dmg version" is in the output.
        if "dmg version" not in output:
            errors.append("dmg version is not in the output! {}".format(output))

        result = re.findall(r"dmg version ([\d.]+)", output)
        if not result:
            errors.append("Failed to obtain dmg version! {}".format(output))
        else:
            dmg_version = result[0]
            self.log.info("dmg version = %s", dmg_version)

        # Get daos version.
        daos_cmd = self.get_daos_command()
        output = daos_cmd.version().stdout.decode("utf-8")

        # Verify that "daos version" is in the output.
        if "daos version" not in output:
            errors.append("daos version is not in the output! {}".format(output))

        result = re.findall(r"daos version ([\d.]+)", output)
        if not result:
            errors.append("Failed to obtain daos version! {}".format(output))
        else:
            daos_version = result[0]
            self.log.info("daos version = %s", daos_version)

        # Get daos_agent version.
        daos_agent_cmd = "daos_agent version"
        output = run_pcmd(hosts=self.hostlist_servers, command=daos_agent_cmd)
        stdout = output[0]["stdout"][0]

        # Verify that "DAOS Agent" is in the output.
        if "DAOS Agent" not in stdout:
            errors.append("DAOS Agent is not in the output! {}".format(stdout))

        result = re.findall(r"DAOS Agent v([\d.]+)", stdout)
        if not result:
            errors.append("Failed to obtain daos_agent version! {}".format(output))
        else:
            daos_agent_version = result[0]
            self.log.info("daos_agent version = %s", daos_agent_version)

        # Get daos_server version
        daos_server_cmd = "daos_server version"
        output = run_pcmd(hosts=self.hostlist_servers, command=daos_server_cmd)
        stdout = output[0]["stdout"][0]

        # Verify that "DAOS Control Server" is in the output.
        if "DAOS Control Server" not in stdout:
            errors.append("DAOS Control Server is not in the output! {}".format(stdout))

        result = re.findall(r"DAOS Control Server v([\d.]+)", stdout)
        if not result:
            errors.append("Failed to obtain daos_server version! {}".format(output))
        else:
            daos_server_version = result[0]
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

        if errors:
            self.fail("\n---- Errors detected! ----\n{}".format("\n".join(errors)))
