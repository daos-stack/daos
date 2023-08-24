"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import socket
import re
import os
from getpass import getuser

from ClusterShell.NodeSet import NodeSet

from command_utils_base import \
    FormattedParameter, EnvironmentVariables, \
    CommonConfig, CommandWithParameters
from exception_utils import CommandFailure
from command_utils import YamlCommand, CommandWithSubCommand, SubprocessManager
from general_utils import get_log_file, run_pcmd, get_default_config_file
from agent_utils_params import \
    DaosAgentTransportCredentials, DaosAgentYamlParameters
from run_utils import run_remote


def include_local_host(hosts):
    """Ensure the local host is included in the specified host list.

    Args:
        hosts (NodeSet): list of hosts

    Returns:
        NodeSet: list of hosts including the local host

    """
    with_localhost = NodeSet(socket.gethostname().split('.', 1)[0])
    with_localhost.add(hosts)
    return with_localhost


def get_agent_command(group, cert_dir, bin_dir, config_file, config_temp=None):
    """Get the daos_agent command object to manage.

    Args:
        group (str): daos_server group name
        cert_dir (str): directory in which to copy certificates
        bin_dir (str): location of the daos_server executable
        config_file (str): configuration file name and path
        config_temp (str, optional): file name and path to use to generate the
            configuration file locally and then copy it to all the hosts using
            the config_file specification. Defaults to None, which creates and
            utilizes the file specified by config_file.

    Returns:
        DaosServerCommand: the daos_server command object

    """
    transport_config = DaosAgentTransportCredentials(cert_dir)
    common_config = CommonConfig(group, transport_config)
    config = DaosAgentYamlParameters(config_file, common_config)
    command = DaosAgentCommand(bin_dir, config)
    if config_temp:
        # Setup the DaosAgentCommand to write the config file data to the
        # temporary file and then copy the file to all the hosts using the
        # assigned filename
        command.temporary_file = config_temp
    return command


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
        super().__init__(
            "/run/agent_config/*", "daos_agent", path, yaml_cfg, timeout)
        self.pattern = "listening on "

        # If specified use the configuration file from the YamlParameters object
        default_yaml_file = None
        if self.yaml is not None and hasattr(self.yaml, "filename"):
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
        super().get_params(test)

        # Run daos_agent with test variant specific log file names if specified
        self.yaml.update_log_file(getattr(test, "agent_log"))

    def get_sub_command_class(self):
        """Get the daos_agent sub command object based on the sub-command."""
        if self.sub_command.value == "dump-attachinfo":
            self.sub_command_class = self.DumpAttachInfoSubCommand()
        if self.sub_command.value == "support":
            self.sub_command_class = self.SupportSubCommand()
        else:
            self.sub_command_class = None

    class DumpAttachInfoSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_agent dump-attachinfo sub command."""

        def __init__(self):
            """Create a daos_agent dump-attachinfo subcommand object."""
            super().__init__(
                "/run/daos_agent/dump-attachinfo/*", "dump-attachinfo")

            self.output = FormattedParameter("--output {}", None)

    class SupportSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_agent support sub command."""

        def __init__(self):
            """Create a daos_agent support subcommand object."""
            super().__init__("/run/daos_agent/support/*", "support")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the daos_agent support sub command object."""
            if self.sub_command.value == "collect-log":
                self.sub_command_class = self.CollectlogSubCommand()
            else:
                self.sub_command_class = None

        class CollectlogSubCommand(CommandWithParameters):
            """Defines an object for the daos_agent support collect-log command."""

            def __init__(self):
                """Create a daos_agent support collect-log command object."""
                super().__init__("/run/daos_agent/support/*", "collect-log")
                self.stop_on_error = FormattedParameter("--stop-on-error", False)
                self.target_folder = FormattedParameter("--target-folder={}", None)
                self.archive = FormattedParameter("--archive", False)
                self.extra_logs_dir = FormattedParameter("--extra-logs-dir={}", None)
                self.target_host = FormattedParameter("--target-host={}", None)

    def dump_attachinfo(self, output="uri.txt"):
        """Write CaRT attachinfo file.

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

    def support_collect_log(self, **kwargs):
        """support collect-log command run.

        Args:
            kwargs (dic): option to pass to support collect-log command

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos_agent support collect-log command fails.

        """
        self.set_command(("support", "collect-log"), **kwargs)
        return self._get_json_result()

    def get_user_file(self):
        """Get the file defined in the yaml file that must be owned by the user.

        Returns:
            str: file defined in the yaml file that must be owned by the user

        """
        return self.get_config_value("runtime_dir")


class DaosAgentManager(SubprocessManager):
    """Manages the daos_agent execution on one or more hosts."""

    def __init__(self, group, bin_dir, cert_dir, config_file, config_temp=None,
                 manager="Orterun", outputdir=None):
        """Initialize a DaosAgentManager object.

        Args:
            group (str): daos_server group name
            bin_dir (str): directory from which to run daos_agent
            cert_dir (str): directory in which to copy certificates
            config_file (str): daos_agent configuration file name and path
            config_temp (str, optional): file name and path used to generate
                the daos_agent configuration file locally and copy it to all
                the hosts using the config_file specification. Defaults to None.
            manager (str, optional): the name of the JobManager class used to
                manage the YamlCommand defined through the "job" attribute.
                Defaults to "Orterun".
            outputdir (str, optional): path to avocado test outputdir. Defaults
                to None.
        """
        agent_command = get_agent_command(
            group, cert_dir, bin_dir, config_file, config_temp)
        super().__init__(agent_command, manager)

        # Set the correct certificate file ownership
        if manager == "Systemctl":
            self.manager.job.certificate_owner = "daos_agent"

        # Set default agent debug levels
        env_vars = {
            "D_LOG_MASK": "DEBUG,RPC=ERR",
            "DD_MASK": "mgmt,io,md,epc,rebuild",
            "D_LOG_FILE_APPEND_PID": "1",
            "COVFILE": "/tmp/test.cov",
        }
        self.manager.assign_environment_default(EnvironmentVariables(env_vars))
        self.attachinfo = None
        self.outputdir = outputdir

    def _set_hosts(self, hosts, path, slots):
        """Set the hosts used to execute the daos command.

        Update the number of daos_agents to expect in the process output.

        Args:
            hosts (list): list of hosts on which to run the command
            path (str): path in which to create the hostfile
            slots (int): number of slots per host to specify in the hostfile
        """
        super()._set_hosts(hosts, path, slots)

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

        # Verify the socket directory exists when using a non-systemctl manager
        self.verify_socket_directory(getuser())

        super().start()

    def dump_attachinfo(self):
        """Run dump-attachinfo on the daos_agent."""
        self.manager.job.set_sub_command("dump-attachinfo")
        self.manager.job.sudo = True
        self.attachinfo = run_pcmd(self.hosts,
                                   str(self.manager.job))[0]["stdout"]
        self.log.info("Agent attachinfo: %s", self.attachinfo)

    def support_collect_log(self, **kwargs):
        """Collect logs for debug purpose.

        Args:
            stop_on_error (bool, optional): Stop the collect-log command on very first error.
            target (str, optional): Target Folder location to copy logs
            archive (bool, optional): Archive the log/config files
            extra_logs_dir (str, optional): Collect the Logs from given custom directory
            target-host (str, optional): R sync all the logs to target system
        Raises:
            CommandFailure: if the daos_agent command fails.

        Returns:
            RemoteCommandResult: a grouping of the command results from
                the same hosts with the same return status

        """
        cmd = DaosAgentCommand(self.manager.job.command_path)
        cmd.sudo = True
        cmd.debug.value = False
        cmd.config.value = get_default_config_file("agent")
        self.log.info("Support collect-log on clients: %s", str(cmd))
        cmd.set_command(("support", "collect-log"), **kwargs)
        return run_remote(self.log, self.hosts, cmd.with_exports)

    def get_attachinfo_file(self):
        """Run dump-attachinfo on the daos_agent.

        Returns:
            str: the attach info file path

        """
        server_name = self.get_config_value("name")

        self.dump_attachinfo()

        attach_info = self.attachinfo

        # Filter log messages from attachinfo content
        messages = [x for x in attach_info if re.match(r"^(name\s|size\s|all|\d+\s)", x)]
        attach_info_contents = "\n".join(messages)
        attach_info_filename = "{}.attach_info_tmp".format(server_name)

        if len(messages) < 4:
            self.log.info("Malformed attachinfo file: %s", attach_info_contents)
            return None

        # Write an attach_info_tmp file in this directory for cart_ctl to use
        attachinfo_file_path = os.path.join(self.outputdir, attach_info_filename)

        with open(attachinfo_file_path, 'w') as file_handle:
            file_handle.write(attach_info_contents)

        return attachinfo_file_path

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
            super().stop()
        except CommandFailure as error:
            messages.append(
                "Error stopping the {} subprocess: {}".format(
                    self.manager.command, error))

        # Kill any leftover processes that may not have been stopped correctly
        self.manager.kill()

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
