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
import socket

from command_utils import BasicParameter, FormattedParameter, YamlParameters
from command_utils import YamlCommand, SubprocessManager, CommandFailure
from command_utils import TransportCredentials


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


class DaosAgentTransportCredentials(TransportCredentials):
    """Transport credentials listing certificates for secure communication."""

    def __init__(self):
        """Initialize a TransportConfig object."""
        super(DaosAgentTransportCredentials, self).__init__(
            "/run/agent_config/transport_config/*", "transport_config")

        # Additional daos_agent transport credential parameters:
        #   - server_name: <str>, e.g. "daos_server"
        #       Name of server accodring to its certificate [daos_agent only]
        #
        self.server_name = BasicParameter(None, "daos_server")


class DaosAgentYamlParameters(YamlParameters):
    """Defines the daos_agent configuration yaml parameters."""

    def __init__(self, filename, common_yaml):
        """Initialize an DaosAgentYamlParameters object.

        Args:
            filename (str): yaml configuration file name
            common_yaml (YamlParameters): [description]
        """
        super(DaosAgentYamlParameters, self).__init__(
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
        super(DaosAgentYamlParameters, self).get_params(test)

        # Override the log file file name with the test log file name
        if hasattr(test, "client_log") and test.client_log is not None:
            self.log_file.value = test.client_log


class DaosAgentCommand(YamlCommand):
    """Defines an object representing a daso_agent command."""

    def __init__(self, path="", yaml_cfg=None, timeout=30):
        """Create a daos_agent command object.

        Args:
            path (str): path to location of daos_agent binary
            yaml_cfg (DaosAgentYamlParameters, optional): agent configuration
                parameters. Defauts to None.
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 60 seconds.
        """
        super(DaosAgentCommand, self).__init__(
            "/run/agent_config/*", "daos_agent", path, yaml_cfg, timeout)
        self.pattern = "Listening on "

        # If specified use the configuration file from the YamlParameters object
        default_yaml_file = None
        if isinstance(self.yaml, YamlParameters):
            default_yaml_file = self.yaml.filename

        # Command line parameters:
        # -d, --debug        Enable debug output
        # -j, --json         Enable JSON output
        # -o, --config-path= Path to agent configuration file
        self.debug = FormattedParameter("--debug", True)
        self.json = FormattedParameter("--json", False)
        self.config = FormattedParameter("--config-path={}", default_yaml_file)

        # Additional daos_agent command line parameters:
        # -i, --insecure     have agent attempt to connect without certificates
        # -s, --runtime_dir= Path to agent communications socket
        # -l, --logfile=     Full path and filename for daos agent log file
        self.insecure = FormattedParameter("--insecure", False)
        self.runtime_dir = FormattedParameter("--runtime_dir=={}")
        self.logfile = FormattedParameter("--logfile={}")


class DaosAgentManager(SubprocessManager):
    """Manages the daos_agent execution on one or more hosts using orterun."""

    def __init__(self, ompi_path, agent_command):
        """Initialize an DaosAgentManager object.

        Args:
            ompi_path (str): path to location of orterun binary.
            agent_command (AgentCommand): server command object
        """
        super(DaosAgentManager, self).__init__(
            "/run/agent_config", agent_command, ompi_path)

    def stop(self):
        """Stop the agent through the runner.

        Raises:
            CommandFailure: if there was an errror stopping the agents.

        """
        self.log.info("Stopping agent orterun command")

        # Maintain a running list of errors detected trying to stop
        messages = []

        # Stop the subprocess running the orterun command
        try:
            super(DaosAgentManager, self).stop()
        except CommandFailure as error:
            messages.append(
                "Error stopping the orterun subprocess: {}".format(error))

        # Kill any leftover processes that may not have been stopped correctly
        self.kill()

        # Report any errors after all stop actions have been attempted
        if len(messages) > 0:
            raise CommandFailure(
                "Failed to stop agents:\n  {}".format("\n  ".join(messages)))
