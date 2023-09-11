'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import re
import json

from apricot import TestWithServers
from general_utils import run_pcmd, report_errors, append_error
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
        output = run_pcmd(hosts=self.hostlist_servers, command=rpm_command)
        self.log.debug("RPM output = %s", output)
        rc = output[0]["exit_status"]
        stdout = output[0]["stdout"]
        if rc != 0:
            report_errors(self, ["DAOS RPMs not properly installed: rc={}".format(rc)])
        rpm_version = None
        for rpm in stdout:
            result = re.findall(r"daos-server-[tests-|tests_openmpi-]*([\d.]+)", rpm)
            if result:
                rpm_version = result[0]
                break
        if not result:
            report_errors(self, ["RPM version could not be defined"])
        self.log.info("RPM version = %s", rpm_version)

        # Remove configuration files
        cleanup_cmds = [
            "sudo find /etc/daos/certs -type f -delete -print",
            "sudo rm -fv /etc/daos/daos_server.yml /etc/daos/daos_control.yml"
            "     /etc/daos/daos_agent.yml",
        ]
        for cmd in cleanup_cmds:
            run_pcmd(hosts=self.hostlist_servers, command=cmd)

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
        output = run_pcmd(hosts=self.hostlist_servers, command=daos_agent_cmd)
        self.log.debug("DAOS Agent output = %s", output)
        rc = output[0]["exit_status"]
        stdout = output[0]["stdout"]
        if rc != 0:
            msg = "DAOS Agent not properly installed: rc={}".format(rc)
            append_error(errors, msg, stdout)
        else:
            self.log.info("DAOS Agent stdout = %s", "".join(stdout))
            daos_agent_version = json.loads("".join(stdout))["response"]["version"]
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
