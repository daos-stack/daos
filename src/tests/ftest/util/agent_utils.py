#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from logging import getLogger
import socket

from command_utils import BasicParameter, FormattedParameter, YamlParameters
from command_utils import DaosYamlCommand, SubprocessManager
from general_utils import pcmd


def include_local_host(hosts):
    """Ensure the local host is included in the specified host list.

    Args:
        hosts (list): list of hosts

    Returns:
        list: list of hosts including the local host

    """
    local_host = socket.gethostname().split('.', 1)[0]
    if hosts is None:
        hosts = [local_host]
    elif local_host not in hosts:
        hosts.append(local_host)
    return hosts


def stop_agent_processes(hosts):
    """Stop the daos agent processes on the specified list of hosts.

    Args:
        hosts (list): hosts on which to stop the daos agent processes
    """
    log = getLogger()
    log.info("Killing any agent processes on %s", hosts)
    if hosts is not None:
        pattern = "'(daos_agent)'"
        commands = [
            "if pgrep --list-name {}".format(pattern),
            "then sudo pkill {}".format(pattern),
            "if pgrep --list-name {}".format(pattern),
            "then sleep 5",
            "pkill --signal KILL {}".format(pattern),
            "fi",
            "fi",
            "exit 0",
        ]
        pcmd(hosts, "; ".join(commands), False, None, None)


class AgentYamlParameters(YamlParameters):
    """Defines the daos_agent configuration yaml parameters."""

    def __init__(self, filename, common_yaml):
        """Initialize an AgentYamlParameters object.

        Args:
            filename (str): yaml configuration file name
            common_yaml (YamlParameters): [description]
        """
        super(AgentYamlParameters, self).__init__(
            "/run/agent_config/*", filename, None, common_yaml)

        # daos_agent parameters:
        #   - runtime_dir: <str>, e.g. /var/run/daos_agent
        #       Use the given directory for creating unix domain sockets
        #   - log_file: <str>, e.g. /tmp/daos_agent.log
        #       Full path and name of the DAOS agent logfile.
        self.runtime_dir = BasicParameter(None, "/var/run/daos_agent")
        self.log_file = BasicParameter(None, "/tmp/daos_agent.log")

    def get_params(self, test):
        """Get values for the daos agent yaml config file.

        Args:
            test (Test): avocado Test object
        """
        super(AgentYamlParameters, self).get_params(test)

        # Override the log file file name with the test log file name
        if hasattr(test, "client_log") and test.client_log is not None:
            self.log_file.value = test.client_log


class AgentCommand(DaosYamlCommand):
    """Defines an object representing a daso_agent command."""

    def __init__(self, agent_config, path="", timeout=30):
        """Create a daos_agent command object.

        Args:
            agent_config (AgentYamlParameters): agent yaml configuration
            path (str): path to location of daos_agent binary
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 60 seconds.
        """
        super(AgentCommand, self).__init__(
            "/run/agent_config/*", "daos_agent", agent_config, path, timeout)
        self.pattern = "Listening on "

        # Additional daos_agent command line parameters:
        # -i, --insecure     have agent attempt to connect without certificates
        # -s, --runtime_dir= Path to agent communications socket
        # -l, --logfile=     Full path and filename for daos agent log file
        self.insecure = FormattedParameter("--insecure", False)
        self.runtime_dir = FormattedParameter("--runtime_dir=={}")
        self.logfile = FormattedParameter("--logfile={}")


class AgentManager(SubprocessManager):
    """Manages the daos_agent execution on one or more hosts using orterun."""

    def __init__(self, ompi_path, agent_command):
        """Initialize an AgentManager object.

        Args:
            ompi_path (str): path to location of orterun binary.
            agent_command (AgentCommand): server command object
        """
        super(AgentManager, self).__init__(
            "/run/agent_config", agent_command, ompi_path)
