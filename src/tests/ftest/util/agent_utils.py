#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import socket

from command_utils_base import \
    CommandFailure, FormattedParameter, YamlParameters, EnvironmentVariables
from command_utils import YamlCommand, CommandWithSubCommand, SubprocessManager
from general_utils import get_log_file


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
        # Take a copy of hosts to avoid modifying-in-place
        hosts = list(hosts)
        hosts.append(local_host)
    return hosts


class DaosAgentCommand(YamlCommand):
    """Defines an object representing a daos_agent command."""

    def __init__(self, path="", yaml_cfg=None, timeout=15):
        """Create a daos_agent command object.

        Args:
            path (str): path to location of daos_agent binary
            yaml_cfg (DaosAgentYamlParameters, optional): agent configuration
                parameters. Defaults to None.
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 60 seconds.
        """
        super(DaosAgentCommand, self).__init__(
            "/run/agent_config/*", "daos_agent", path, yaml_cfg, timeout)
        self.pattern = "listening on "

        # If specified use the configuration file from the YamlParameters object
        default_yaml_file = None
        if isinstance(self.yaml, YamlParameters):
            default_yaml_file = self.yaml.filename

        # Command line parameters:
        # -d, --debug        Enable debug output
        # -J, --json-logging Enable JSON logging
        # -o, --config-path= Path to agent configuration file
        self.debug = FormattedParameter("--debug", True)
        self.json_logs = FormattedParameter("--json-logging", False)
        self.config = FormattedParameter("--config-path={}", default_yaml_file)

        # Additional daos_agent command line parameters:
        # -i, --insecure     have agent attempt to connect without certificates
        # -s, --runtime_dir= Path to agent communications socket
        # -l, --logfile=     Full path and filename for daos agent log file
        self.insecure = FormattedParameter("--insecure", False)
        self.runtime_dir = FormattedParameter("--runtime_dir=={}")
        self.logfile = FormattedParameter("--logfile={}")

    def get_params(self, test):
        """Get values for the daos command and its yaml config file.

        Args:
            test (Test): avocado Test object
        """
        super(DaosAgentCommand, self).get_params(test)

        # Run daos_agent with test variant specific log file names if specified
        self.yaml.update_log_file(getattr(test, "agent_log"))

    def get_sub_command_class(self):
        """Get the daos_agent sub command object based on the sub-command."""
        if self.sub_command.value == "dump-attachinfo":
            self.sub_command_class = self.DumpAttachInfoSubCommand()
        else:
            self.sub_command_class = None

    class DumpAttachInfoSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_agent dump-attachinfo sub command."""

        def __init__(self):
            """Create a daos_agent dump-attachinfo subcommand object."""
            super(DaosAgentCommand.DumpAttachInfoSubCommand, self).__init__(
                "/run/daos_agent/dump-attachinfo/*", "dump-attachinfo")

            self.output = FormattedParameter("--output {}", None)

    def dump_attachinfo(self, output="uri.txt"):
        """Write CaRT attachinfo file

        Args:
            output (str): File to which attachinfo dump should be written.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos_agent dump-attachinfo command fails.

        """
        self.set_sub_command("dump-attachinfo")
        self.sub_command_class.output.value = output
        return self._get_result()


class DaosAgentManager(SubprocessManager):
    """Manages the daos_agent execution on one or more hosts."""

    def __init__(self, agent_command, manager="Orterun"):
        """Create a DaosAgentManager object.

        Args:
            agent_command (DaosAgentCommand): daos_agent command class
            manager (str, optional): the name of the JobManager class used to
                manage the YamlCommand defined through the "job" attribute.
                Defaults to "OpenMpi"
        """
        super(DaosAgentManager, self).__init__(agent_command, manager)

        # Set default agent debug levels
        env_vars = {
            "D_LOG_MASK": "DEBUG,RPC=ERR",
            "DD_MASK": "mgmt,io,md,epc,rebuild"
        }
        self.manager.assign_environment_default(EnvironmentVariables(env_vars))

    def _set_hosts(self, hosts, path, slots):
        """Set the hosts used to execute the daos command.

        Update the number of daos_agents to expect in the process output.

        Args:
            hosts (list): list of hosts on which to run the command
            path (str): path in which to create the hostfile
            slots (int): number of slots per host to specify in the hostfile
        """
        super(DaosAgentManager, self)._set_hosts(hosts, path, slots)

        # Update the expected number of messages to reflect the number of
        # daos_agent processes that will be started by the command
        self.manager.job.pattern_count = len(self._hosts)

    def start(self):
        """Start the agent through the job manager."""
        self.log.info(
            "<AGENT> Starting daos_agent on %s with %s",
            self._hosts, self.manager.command)
        # Copy certificates
        self.manager.job.copy_certificates(
            get_log_file("daosCA/certs"), self._hosts)
        super(DaosAgentManager, self).start()

    def stop(self):
        """Stop the agent through the job manager.

        Raises:
            CommandFailure: if there was an error stopping the agents.

        """
        self.log.info("<AGENT> Stopping agent %s command", self.manager.command)

        # Maintain a running list of errors detected trying to stop
        messages = []

        # Stop the subprocess running the manager command
        try:
            super(DaosAgentManager, self).stop()
        except CommandFailure as error:
            messages.append(
                "Error stopping the {} subprocess: {}".format(
                    self.manager.command, error))

        # Kill any leftover processes that may not have been stopped correctly
        self.kill()

        # Report any errors after all stop actions have been attempted
        if messages:
            raise CommandFailure(
                "Failed to stop agents:\n  {}".format("\n  ".join(messages)))

    def get_user_file(self):
        """Get the file defined in the yaml file that must be owned by the user.

        Returns:
            str: file defined in the yaml file that must be owned by the user

        """
        return self.get_config_value("runtime_dir")
