#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines
from datetime import datetime
from getpass import getuser
import os
import socket
import time

from avocado import fail_on
from ClusterShell.NodeSet import NodeSet

from command_utils_base import \
    CommandFailure, FormattedParameter, YamlParameters, CommandWithParameters, \
    CommonConfig
from command_utils import YamlCommand, CommandWithSubCommand, SubprocessManager
from general_utils import pcmd, get_log_file, human_to_bytes, bytes_to_human, \
    convert_list
from dmg_utils import get_dmg_command
from server_utils_params import \
    DaosServerTransportCredentials, DaosServerYamlParameters


def get_server_command(group, cert_dir, bin_dir, config_file, config_temp=None):
    """Get the daos_server command object to manage.

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
    transport_config = DaosServerTransportCredentials(cert_dir)
    common_config = CommonConfig(group, transport_config)
    config = DaosServerYamlParameters(config_file, common_config)
    command = DaosServerCommand(bin_dir, config)
    if config_temp:
        # Setup the DaosServerCommand to write the config file data to the
        # temporary file and then copy the file to all the hosts using the
        # assigned filename
        command.temporary_file = config_temp
    return command


class ServerFailed(Exception):
    """Server didn't start/stop properly."""


class DaosServerCommand(YamlCommand):
    """Defines an object representing the daos_server command."""

    NORMAL_PATTERN = "DAOS I/O Engine.*started"
    FORMAT_PATTERN = "(SCM format required)(?!;)"
    REFORMAT_PATTERN = "Metadata format required"

    DEFAULT_CONFIG_FILE = os.path.join(os.sep, "etc", "daos", "daos_server.yml")

    def __init__(self, path="", yaml_cfg=None, timeout=30):
        """Create a daos_server command object.

        Args:
            path (str): path to location of daos_server binary.
            yaml_cfg (YamlParameters, optional): yaml configuration parameters.
                Defaults to None.
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 30 seconds.
        """
        super(DaosServerCommand, self).__init__(
            "/run/daos_server/*", "daos_server", path, yaml_cfg, timeout)
        self.pattern = self.NORMAL_PATTERN

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
        self.config = FormattedParameter("--config={}", default_yaml_file)

        # Additional daos_server command line parameters:
        #     --allow-proxy  Allow proxy configuration via environment
        self.allow_proxy = FormattedParameter("--allow-proxy", False)

        # Used to override the sub_command.value parameter value
        self.sub_command_override = None

        # Include the daos_engine command launched by the daos_server
        # command.
        self._exe_names.append("daos_engine")

    def get_sub_command_class(self):
        # pylint: disable=redefined-variable-type
        """Get the daos_server sub command object based upon the sub-command."""
        if self.sub_command_override is not None:
            # Override the sub_command parameter
            sub_command = self.sub_command_override
        else:
            # Use the sub_command parameter from the test's yaml
            sub_command = self.sub_command.value

        # Available daos_server sub-commands:
        #   network  Perform network device scan based on fabric provider
        #   start    Start daos_server
        #   storage  Perform tasks related to locally-attached storage
        if sub_command == "network":
            self.sub_command_class = self.StartSubCommand()
        elif sub_command == "start":
            self.sub_command_class = self.StartSubCommand()
        elif sub_command == "storage":
            self.sub_command_class = self.StorageSubCommand()
        else:
            self.sub_command_class = None

    def get_params(self, test):
        """Get values for the daos command and its yaml config file.

        Args:
            test (Test): avocado Test object
        """
        super(DaosServerCommand, self).get_params(test)

        # Run daos_server with test variant specific log file names if specified
        self.yaml.update_log_files(
            getattr(test, "control_log"),
            getattr(test, "helper_log"),
            getattr(test, "server_log")
        )

    def update_pattern(self, mode, host_qty):
        """Update the pattern used to determine if the daos_server started.

        Args:
            mode (str): operation mode for the 'daos_server start' command
            host_qty (int): number of hosts issuing 'daos_server start'
        """
        if mode == "format":
            self.pattern = self.FORMAT_PATTERN
        elif mode == "reformat":
            self.pattern = self.REFORMAT_PATTERN
        else:
            self.pattern = self.NORMAL_PATTERN
        self.pattern_count = host_qty * len(self.yaml.engine_params)

    @property
    def using_nvme(self):
        """Is the daos command setup to use NVMe devices.

        Returns:
            bool: True if NVMe devices are configured; False otherwise

        """
        value = False
        if isinstance(self.yaml, YamlParameters):
            value = self.yaml.using_nvme
        return value

    @property
    def using_dcpm(self):
        """Is the daos command setup to use SCM devices.

        Returns:
            bool: True if SCM devices are configured; False otherwise

        """
        value = False
        if isinstance(self.yaml, YamlParameters):
            value = self.yaml.using_dcpm
        return value

    class NetworkSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_server network sub command."""

        def __init__(self):
            """Create a network subcommand object."""
            super(DaosServerCommand.NetworkSubCommand, self).__init__(
                "/run/daos_server/network/*", "network")

        def get_sub_command_class(self):
            """Get the daos_server network sub command object."""
            # Available daos_server network sub-commands:
            #   list  List all known OFI providers that are understood by 'scan'
            #   scan  Scan for network interface devices on local server
            if self.sub_command.value == "scan":
                self.sub_command_class = self.ScanSubCommand()
            else:
                self.sub_command_class = None

        class ScanSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server network scan command."""

            def __init__(self):
                """Create a network scan subcommand object."""
                super(
                    DaosServerCommand.NetworkSubCommand.ScanSubCommand,
                    self).__init__(
                        "/run/daos_server/network/scan/*", "scan")

                # daos_server network scan command options:
                #   --provider=     Filter device list to those that support the
                #                   given OFI provider (default is the provider
                #                   specified in daos_server.yml)
                #   --all           Specify 'all' to see all devices on all
                #                   providers.  Overrides --provider
                self.provider = FormattedParameter("--provider={}")
                self.all = FormattedParameter("--all", False)

    class StartSubCommand(CommandWithParameters):
        """Defines an object representing a daos_server start sub command."""

        def __init__(self):
            """Create a start subcommand object."""
            super(DaosServerCommand.StartSubCommand, self).__init__(
                "/run/daos_server/start/*", "start")

            # daos_server start command options:
            #   --port=                 Port for the gRPC management interface
            #                           to listen on
            #   --storage=              Storage path
            #   --modules=              List of server modules to load
            #   --targets=              number of targets to use (default use
            #                           all cores)
            #   --xshelpernr=           number of helper XS per VOS target
            #   --firstcore=            index of first core for service thread
            #                           (default: 0)
            #   --group=                Server group name
            #   --socket_dir=           Location for all daos_server and
            #                           daos_engine sockets
            #   --insecure              allow for insecure connections
            #   --recreate-superblocks  recreate missing superblocks rather than
            #                           failing
            self.port = FormattedParameter("--port={}")
            self.storage = FormattedParameter("--storage={}")
            self.modules = FormattedParameter("--modules={}")
            self.targets = FormattedParameter("--targets={}")
            self.xshelpernr = FormattedParameter("--xshelpernr={}")
            self.firstcore = FormattedParameter("--firstcore={}")
            self.group = FormattedParameter("--group={}")
            self.sock_dir = FormattedParameter("--socket_dir={}")
            self.insecure = FormattedParameter("--insecure", False)
            self.recreate = FormattedParameter("--recreate-superblocks", False)

    class StorageSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_server storage sub command."""

        def __init__(self):
            """Create a storage subcommand object."""
            super(DaosServerCommand.StorageSubCommand, self).__init__(
                "/run/daos_server/storage/*", "storage")

        def get_sub_command_class(self):
            """Get the daos_server storage sub command object."""
            # Available sub-commands:
            #   prepare  Prepare SCM and NVMe storage attached to remote servers
            #   scan     Scan SCM and NVMe storage attached to local server
            if self.sub_command.value == "prepare":
                self.sub_command_class = self.PrepareSubCommand()
            else:
                self.sub_command_class = None

        class PrepareSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server storage prepare command."""

            def __init__(self):
                """Create a storage subcommand object."""
                super(
                    DaosServerCommand.StorageSubCommand.PrepareSubCommand,
                    self).__init__(
                        "/run/daos_server/storage/prepare/*", "prepare")

                # daos_server storage prepare command options:
                #   --pci-whitelist=    Whitespace separated list of PCI
                #                       devices (by address) to be unbound from
                #                       Kernel driver and used with SPDK
                #                       (default is all PCI devices).
                #   --hugepages=        Number of hugepages to allocate (in MB)
                #                       for use by SPDK (default 1024)
                #   --target-user=      User that will own hugepage mountpoint
                #                       directory and vfio groups.
                #   --nvme-only         Only prepare NVMe storage.
                #   --scm-only          Only prepare SCM.
                #   --reset             Reset SCM modules to memory mode after
                #                       removing namespaces. Reset SPDK
                #                       returning NVMe device bindings back to
                #                       kernel modules.
                #   --force             Perform format without prompting for
                #                       confirmation
                self.pci_whitelist = FormattedParameter("--pci-whitelist={}")
                self.hugepages = FormattedParameter("--hugepages={}")
                self.target_user = FormattedParameter("--target-user={}")
                self.nvme_only = FormattedParameter("--nvme-only", False)
                self.scm_only = FormattedParameter("--scm-only", False)
                self.reset = FormattedParameter("--reset", False)
                self.force = FormattedParameter("--force", False)


class DaosServerManager(SubprocessManager):
    # pylint: disable=too-many-public-methods
    """Manages the daos_server execution on one or more hosts."""

    # Mapping of environment variable names to daos_server config param names
    ENVIRONMENT_VARIABLE_MAPPING = {
        "CRT_PHY_ADDR_STR": "provider",
        "OFI_INTERFACE": "fabric_iface",
        "OFI_PORT": "fabric_iface_port",
    }

    # Defined in telemetry_common.h
    D_TM_SHARED_MEMORY_KEY = 0x10242048

    def __init__(self, group, bin_dir,
                 svr_cert_dir, svr_config_file, dmg_cert_dir, dmg_config_file,
                 svr_config_temp=None, dmg_config_temp=None, manager="Orterun"):
        """Initialize a DaosServerManager object.

        Args:
            group (str): daos_server group name
            binary_dir (str): directory from which to run daos_server/dmg
            svr_cert_dir (str): directory in which to copy server certificates
            svr_config_file (str): daos_server configuration file name and path
            dmg_cert_dir (str): directory in which to copy dmg certificates
            dmg_config_file (str): dmg configuration file name and path
            svr_config_temp (str, optional): file name and path used to generate
                the daos_server configuration file locally and copy it to all
                the hosts using the config_file specification. Defaults to None.
            dmg_config_temp (str, optional): file name and path used to generate
                the dmg command configuration file locally and copy it to all
                the hosts using the config_file specification. Defaults to None.
            manager (str, optional): the name of the JobManager class used to
                manage the YamlCommand defined through the "job" attribute.
                Defaults to "Orterun".
        """
        server_command = get_server_command(
            group, svr_cert_dir, bin_dir, svr_config_file, svr_config_temp)
        super(DaosServerManager, self).__init__(server_command, manager)
        self.manager.job.sub_command_override = "start"

        # Dmg command to access this group of servers which will be configured
        # to access the daos_servers when they are started
        self.dmg = get_dmg_command(
            group, dmg_cert_dir, bin_dir, dmg_config_file, dmg_config_temp)

        # Set the correct certificate file ownership
        if manager == "Systemctl":
            self.manager.job.certificate_owner = "daos_server"
            self.dmg.certificate_owner = getuser()

        # An internal dictionary used to define the expected states of each
        # server rank when checking their states. It will be populated with
        # the dictionary output of DmgCommand.system_query() when any of the
        # following methods are called:
        #   - start()
        #   - verify_expected_states(set_expected=True)
        # Individual rank states may also be updated by calling the
        # update_expected_states() method. This is required to mark any rank
        # stopped by a test with the correct state to avoid errors being raised
        # during tearDown().
        self._expected_states = {}

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Use the yaml file parameter values to assign the server command and
        orterun command parameters.

        Args:
            test (Test): avocado Test object
        """
        super(DaosServerManager, self).get_params(test)
        # Get the values for the dmg parameters
        self.dmg.get_params(test)

    def prepare_dmg(self, hosts=None):
        """Prepare the dmg command prior to its execution.

        This is only required to be called once and is included as part of
        calling prepare() and start().

        It should be called idependently when a test variant is using servers
        started by a previous test variant.

        Args:
            hosts (list, optional): dmg hostlist value. Defaults to None which
                results in using the 'access_points' host list.
        """
        self._prepare_dmg_certificates()
        self._prepare_dmg_hostlist(hosts)

    def _prepare_dmg_certificates(self):
        """Set up dmg certificates."""
        local_host = socket.gethostname().split('.', 1)[0]
        self.dmg.copy_certificates(
            get_log_file("daosCA/certs"), local_host.split())

    def _prepare_dmg_hostlist(self, hosts=None):
        """Set up the dmg command host list to use the specified hosts.

        Args:
            hosts (list, optional): dmg hostlist value. Defaults to None which
                results in using the 'access_points' host list.
        """
        if hosts is None:
            hosts = self.get_config_value("access_points")
        self.dmg.hostlist = hosts

    def prepare(self, storage=True):
        """Prepare to start daos_server.

        Args:
            storage (bool, optional): whether or not to prepare dcpm/nvme
                storage. Defaults to True.
        """
        self.log.info(
            "<SERVER> Preparing to start daos_server on %s with %s",
            self._hosts, self.manager.command)

        # Create the daos_server yaml file
        self.manager.job.temporary_file_hosts = self._hosts
        self.manager.job.create_yaml_file()

        # Copy certificates
        self.manager.job.copy_certificates(
            get_log_file("daosCA/certs"), self._hosts)
        self._prepare_dmg_certificates()

        # Prepare dmg for running storage format on all server hosts
        self._prepare_dmg_hostlist(self._hosts)
        if not self.dmg.yaml:
            # If using a dmg config file, transport security was
            # already configured.
            self.dmg.insecure.update(
                self.get_config_value("allow_insecure"), "dmg.insecure")

        # Kill any daos servers running on the hosts
        self.kill()

        # Clean up any files that exist on the hosts
        self.clean_files()

        if storage:
            # Prepare server storage
            if self.manager.job.using_nvme or self.manager.job.using_dcpm:
                self.log.info("Preparing storage in <format> mode")
                self.prepare_storage("root")
                if hasattr(self.manager, "mca"):
                    self.manager.mca.update(
                        {"plm_rsh_args": "-l root"}, "orterun.mca", True)

        # Verify the socket directory exists when using a non-systemctl manager
        self.verify_socket_directory(getuser())

    def clean_files(self, verbose=True):
        """Clean up the daos server files.

        Args:
            verbose (bool, optional): display clean commands. Defaults to True.
        """
        clean_commands = []
        for index, engine_params in \
                enumerate(self.manager.job.yaml.engine_params):
            scm_mount = engine_params.get_value("scm_mount")
            self.log.info("Cleaning up the %s directory.", str(scm_mount))

            # Remove the superblocks
            cmd = "sudo rm -fr {}/*".format(scm_mount)
            if cmd not in clean_commands:
                clean_commands.append(cmd)

            # Remove the shared memory segment associated with this io server
            cmd = "sudo ipcrm -M {}".format(self.D_TM_SHARED_MEMORY_KEY + index)
            clean_commands.append(cmd)

            # Dismount the scm mount point
            cmd = "while sudo umount {}; do continue; done".format(scm_mount)
            if cmd not in clean_commands:
                clean_commands.append(cmd)

            if self.manager.job.using_dcpm:
                scm_list = engine_params.get_value("scm_list")
                if isinstance(scm_list, list):
                    self.log.info(
                        "Cleaning up the following device(s): %s.",
                        ", ".join(scm_list))
                    # Umount and wipefs the dcpm device
                    cmd_list = [
                        "for dev in {}".format(" ".join(scm_list)),
                        "do mount=$(lsblk $dev -n -o MOUNTPOINT)",
                        "if [ ! -z $mount ]",
                        "then while sudo umount $mount",
                        "do continue",
                        "done",
                        "fi",
                        "sudo wipefs -a $dev",
                        "done"
                    ]
                    cmd = "; ".join(cmd_list)
                    if cmd not in clean_commands:
                        clean_commands.append(cmd)

        pcmd(self._hosts, "; ".join(clean_commands), verbose)

    def prepare_storage(self, user, using_dcpm=None, using_nvme=None):
        """Prepare the server storage.

        Args:
            user (str): username
            using_dcpm (bool, optional): override option to prepare scm storage.
                Defaults to None, which uses the configuration file to determine
                if scm storage should be formatted.
            using_nvme (bool, optional): override option to prepare nvme
                storage. Defaults to None, which uses the configuration file to
                determine if nvme storage should be formatted.

        Raises:
            ServerFailed: if there was an error preparing the storage

        """
        cmd = DaosServerCommand(self.manager.job.command_path)
        cmd.sudo = False
        cmd.debug.value = False
        cmd.set_sub_command("storage")
        cmd.sub_command_class.set_sub_command("prepare")
        cmd.sub_command_class.sub_command_class.target_user.value = user
        cmd.sub_command_class.sub_command_class.force.value = True

        # Use the configuration file settings if no overrides specified
        if using_dcpm is None:
            using_dcpm = self.manager.job.using_dcpm
        if using_nvme is None:
            using_nvme = self.manager.job.using_nvme

        if using_dcpm and not using_nvme:
            cmd.sub_command_class.sub_command_class.scm_only.value = True
        elif not using_dcpm and using_nvme:
            cmd.sub_command_class.sub_command_class.nvme_only.value = True

        if using_nvme:
            hugepages = self.get_config_value("nr_hugepages")
            cmd.sub_command_class.sub_command_class.hugepages.value = hugepages

        self.log.info("Preparing DAOS server storage: %s", str(cmd))
        result = pcmd(self._hosts, str(cmd), timeout=40)
        if len(result) > 1 or 0 not in result:
            dev_type = "nvme"
            if using_dcpm and using_nvme:
                dev_type = "dcpm & nvme"
            elif using_dcpm:
                dev_type = "dcpm"
            raise ServerFailed("Error preparing {} storage".format(dev_type))

    def detect_format_ready(self, reformat=False):
        """Detect when all the daos_servers are ready for storage format.

        Args:
            reformat (bool, optional): whether or detect reformat (True) or
                format (False) messages. Defaults to False.

        Raises:
            ServerFailed: if there was an error starting the servers.

        """
        f_type = "format" if not reformat else "reformat"
        self.log.info("<SERVER> Waiting for servers to be ready for %s", f_type)
        self.manager.job.update_pattern(f_type, len(self._hosts))
        try:
            self.manager.run()
        except CommandFailure as error:
            self.kill()
            raise ServerFailed(
                "Failed to start servers before format: {}".format(error))

    def detect_engine_start(self, host_qty=None):
        """Detect when all the engines have started.

        Args:
            host_qty (int): number of servers expected to have been started.

        Raises:
            ServerFailed: if there was an error starting the servers after
                formatting.

        """
        if host_qty is None:
            hosts_qty = len(self._hosts)
        self.log.info("<SERVER> Waiting for the daos_engine to start")
        self.manager.job.update_pattern("normal", hosts_qty)
        if not self.manager.check_subprocess_status(self.manager.process):
            self.kill()
            raise ServerFailed("Failed to start servers after format")

        # Update the dmg command host list to work with pool create/destroy
        self._prepare_dmg_hostlist()

        # Define the expected states for each rank
        self._expected_states = self.system_query()

    def reset_storage(self):
        """Reset the server storage.

        Raises:
            ServerFailed: if there was an error resetting the storage

        """
        cmd = DaosServerCommand(self.manager.job.command_path)
        cmd.sudo = False
        cmd.debug.value = False
        cmd.set_sub_command("storage")
        cmd.sub_command_class.set_sub_command("prepare")
        cmd.sub_command_class.sub_command_class.nvme_only.value = True
        cmd.sub_command_class.sub_command_class.reset.value = True
        cmd.sub_command_class.sub_command_class.force.value = True

        self.log.info("Resetting DAOS server storage: %s", str(cmd))
        result = pcmd(self._hosts, str(cmd), timeout=120)
        if len(result) > 1 or 0 not in result:
            raise ServerFailed("Error resetting NVMe storage")

    def set_scm_mount_ownership(self, user=None, verbose=False):
        """Set the ownership to the specified user for each scm mount.

        Args:
            user (str, optional): user name. Defaults to None - current user.
            verbose (bool, optional): display commands. Defaults to False.

        """
        user = getuser() if user is None else user

        cmd_list = set()
        for engine_params in self.manager.job.yaml.engine_params:
            scm_mount = engine_params.scm_mount.value

            # Support single or multiple scm_mount points
            if not isinstance(scm_mount, list):
                scm_mount = [scm_mount]

            self.log.info("Changing ownership to %s for: %s", user, scm_mount)
            cmd_list.add(
                "sudo chown -R {0}:{0} {1}".format(user, " ".join(scm_mount)))

        if cmd_list:
            pcmd(self._hosts, "; ".join(cmd_list), verbose)

    def start(self):
        """Start the server through the job manager."""
        # Prepare the servers
        self.prepare()

        # Start the servers and wait for them to be ready for storage format
        self.detect_format_ready()

        # Format storage and wait for server to change ownership
        self.log.info(
            "<SERVER> Formatting hosts: <%s>", self.dmg.hostlist)
        # Temporarily increasing timeout to avoid CI errors until DAOS-5764 can
        # be further investigated.
        self.dmg.storage_format(timeout=40)

        # Wait for all the engines to start
        self.detect_engine_start()

        return True

    def stop(self):
        """Stop the server through the runner."""
        self.log.info(
            "<SERVER> Stopping server %s command", self.manager.command)

        # Maintain a running list of errors detected trying to stop
        messages = []

        # Stop the subprocess running the job manager command
        try:
            super(DaosServerManager, self).stop()
        except CommandFailure as error:
            messages.append(
                "Error stopping the {} subprocess: {}".format(
                    self.manager.command, error))

        # Kill any leftover processes that may not have been stopped correctly
        self.kill()

        if self.manager.job.using_nvme:
            # Reset the storage
            try:
                self.reset_storage()
            except ServerFailed as error:
                messages.append(str(error))

            # Make sure the mount directory belongs to non-root user
            self.set_scm_mount_ownership()

        # Report any errors after all stop actions have been attempted
        if messages:
            raise ServerFailed(
                "Failed to stop servers:\n  {}".format("\n  ".join(messages)))

    def get_environment_value(self, name):
        """Get the server config value associated with the env variable name.

        Args:
            name (str): environment variable name for which to get a daos_server
                configuration value

        Raises:
            ServerFailed: Unable to find a daos_server configuration value for
                the specified environment variable name

        Returns:
            str: the daos_server configuration value for the specified
                environment variable name

        """
        try:
            setting = self.ENVIRONMENT_VARIABLE_MAPPING[name]

        except IndexError:
            raise ServerFailed(
                "Unknown server config setting mapping for the {} environment "
                "variable!".format(name))

        return self.get_config_value(setting)

    def get_single_system_state(self):
        """Get the current homogeneous DAOS system state.

        Raises:
            ServerFailed: if a single state for all servers is not detected

        Returns:
            str: the current DAOS system state

        """
        data = self.system_query()
        if not data:
            # The regex failed to get the rank and state
            raise ServerFailed(
                "Error obtaining {} output: {}".format(self.dmg, data))
        try:
            states = list(set([data[rank]["state"] for rank in data]))
        except KeyError:
            raise ServerFailed(
                "Unexpected result from {} - missing 'state' key: {}".format(
                    self.dmg, data))
        if len(states) > 1:
            # Multiple states for different ranks detected
            raise ServerFailed(
                "Multiple system states ({}) detected:\n  {}".format(
                    states, data))
        return states[0]

    def check_system_state(self, valid_states, max_checks=1):
        """Check that the DAOS system state is one of the provided states.

        Fail the test if the current state does not match one of the specified
        valid states.  Optionally the state check can loop multiple times,
        sleeping one second between checks, by increasing the number of maximum
        checks.

        Args:
            valid_states (list): expected DAOS system states as a list of
                lowercase strings
            max_checks (int, optional): number of times to check the state.
                Defaults to 1.

        Raises:
            ServerFailed: if there was an error detecting the server state or
                the detected state did not match one of the valid states

        Returns:
            str: the matching valid detected state

        """
        checks = 0
        daos_state = "????"
        while daos_state not in valid_states and checks < max_checks:
            if checks > 0:
                time.sleep(1)
            try:
                daos_state = self.get_single_system_state().lower()
            except ServerFailed as error:
                raise error
            checks += 1
            self.log.info("System state check (%s): %s", checks, daos_state)
        if daos_state not in valid_states:
            raise ServerFailed(
                "Error checking DAOS state, currently neither {} after "
                "{} state check(s)!".format(valid_states, checks))
        return daos_state

    def system_start(self):
        """Start the DAOS I/O Engines.

        Raises:
            ServerFailed: if there was an error starting the servers

        """
        self.log.info("Starting DAOS I/O Engines")
        self.check_system_state(("stopped"))
        self.dmg.system_start()
        if self.dmg.result.exit_status != 0:
            raise ServerFailed(
                "Error starting DAOS:\n{}".format(self.dmg.result))

    def system_stop(self, extra_states=None):
        """Stop the DAOS I/O Engines.

        Args:
            extra_states (list, optional): a list of DAOS system states in
                addition to "started" and "joined" that are verified prior to
                issuing the stop. Defaults to None.

        Raises:
            ServerFailed: if there was an error stopping the servers

        """
        valid_states = ["started", "joined"]
        if extra_states:
            valid_states.extend(extra_states)
        self.log.info("Stopping DAOS I/O Engines")
        self.check_system_state(valid_states)
        self.dmg.system_stop(force=True)
        if self.dmg.result.exit_status != 0:
            raise ServerFailed(
                "Error stopping DAOS:\n{}".format(self.dmg.result))

    def get_available_storage(self):
        """Get the available SCM and NVMe storage.

        Raises:
            ServerFailed: if there was an error stopping the servers

        Returns:
            list: a list of the maximum available SCM and NVMe sizes in bytes

        """
        def get_host_capacity(key, device_names):
            """Get the total storage capacity per host rank.

            Args:
                key (str): the capacity type, e.g. "scm" or "nvme"
                device_names (list): the device names of this capacity type

            Returns:
                dict: a dictionary of total storage capacity per host rank

            """
            host_capacity = {}
            for host in data:
                device_sizes = []
                for device in data[host][key]:
                    if device in device_names:
                        device_sizes.append(
                            human_to_bytes(
                                data[host][key][device]["capacity"]))
                host_capacity[host] = sum(device_sizes)
            return host_capacity

        # Default maximum bytes for SCM and NVMe
        storage = [0, 0]

        using_dcpm = self.manager.job.using_dcpm
        using_nvme = self.manager.job.using_nvme

        if using_dcpm or using_nvme:
            # Stop the DAOS I/O Engines in order to be able to scan the storage
            self.system_stop()

            # Scan all of the hosts for their SCM and NVMe storage
            self._prepare_dmg_hostlist(self._hosts)
            data = self.dmg.storage_scan(verbose=True)
            self._prepare_dmg_hostlist()
            if self.dmg.result.exit_status != 0:
                raise ServerFailed(
                    "Error obtaining DAOS storage:\n{}".format(self.dmg.result))

            # Restart the DAOS I/O Engines
            self.system_start()

        if using_dcpm:
            # Find the sizes of the configured SCM storage
            scm_devices = [
                os.path.basename(path)
                for path in self.get_config_value("scm_list") if path]
            capacity = get_host_capacity("scm", scm_devices)
            for host in sorted(capacity):
                self.log.info("SCM capacity for %s: %s", host, capacity[host])
            # Use the minimum SCM storage across all servers
            storage[0] = capacity[min(capacity, key=capacity.get)]
        else:
            # Use the assigned scm_size
            scm_size = self.get_config_value("scm_size")
            storage[0] = human_to_bytes("{}GB".format(scm_size))

        if using_nvme:
            # Find the sizes of the configured NVMe storage
            capacity = get_host_capacity(
                "nvme", self.get_config_value("bdev_list"))
            for host in sorted(capacity):
                self.log.info("NVMe capacity for %s: %s", host, capacity[host])
            # Use the minimum SCM storage across all servers
            storage[1] = capacity[min(capacity, key=capacity.get)]

        self.log.info(
            "Total available storage:\n  SCM:  %s (%s)\n  NVMe: %s (%s)",
            str(storage[0]), bytes_to_human(storage[0], binary=False),
            str(storage[1]), bytes_to_human(storage[1], binary=False))
        return storage

    def system_query(self):
        """Query the state of the daos_server ranks.

        Returns:
            dict: dictionary of server rank keys, each referencing a dictionary
                of information.  This will be empty if there was error obtaining
                the dmg system query output.

        """
        try:
            data = self.dmg.system_query()
        except CommandFailure:
            data = {}
        return data

    def update_expected_states(self, ranks, state):
        """Update the expected state of the specified server rank.

        Args:
            ranks (object): server ranks to update. Can be a single rank (int),
                multiple ranks (list), or all the ranks (None).
            state (object): new state to assign as the expected state of this
                rank. Can be a str or a list.
        """
        if ranks is None:
            ranks = [key for key in self._expected_states]
        elif not isinstance(ranks, (list, tuple)):
            ranks = [ranks]

        for rank in ranks:
            if rank in self._expected_states:
                self.log.info(
                    "Updating the expected state for rank %s on %s: %s -> %s",
                    rank, self._expected_states[rank]["domain"],
                    self._expected_states[rank]["state"], state)
                self._expected_states[rank]["state"] = state

    def verify_expected_states(self, set_expected=False):
        """Verify that the expected server rank states match the current states.

        Args:
            set_expected (bool, optional): option to update the expected server
                rank states to the current states prior to checking the states.
                Defaults to False.

        Returns:
            dict: a dictionary of whether or not any of the server states were
                not 'expected' (which should warrant an error) and whether or
                the servers require a 'restart' (either due to any unexpected
                states or because at least one servers was found to no longer
                be running)

        """
        status = {"expected": True, "restart": False}
        running_states = ["started", "joined"]
        errored_states = ["errored"]
        errored_hosts = []

        # Get the current state of the servers
        current_states = self.system_query()
        if set_expected:
            # Assign the expected states to the current server rank states
            self.log.info("<SERVER> Assigning expected server states.")
            self._expected_states = current_states.copy()

        # Verify the expected states match the current states
        self.log.info(
            "<SERVER> Verifying server states: group=%s, hosts=%s",
            self.get_config_value("name"), NodeSet.fromlist(self._hosts))
        if current_states:
            log_format = "  %-4s  %-15s  %-36s  %-22s  %-14s  %s"
            self.log.info(
                log_format,
                "Rank", "Host", "UUID", "Expected State", "Current State",
                "Result")
            self.log.info(
                log_format,
                "-" * 4, "-" * 15, "-" * 36, "-" * 22, "-" * 14, "-" * 6)

            # Verify that each expected rank appears in the current states
            for rank in sorted(self._expected_states):
                domain = self._expected_states[rank]["domain"].split(".")
                current_host = domain[0].replace("/", "")
                expected = self._expected_states[rank]["state"]
                if isinstance(expected, (list, tuple)):
                    expected = [item.lower() for item in expected]
                else:
                    expected = [expected.lower()]
                try:
                    current_rank = current_states.pop(rank)
                    current = current_rank["state"].lower()
                except KeyError:
                    current = "not detected"

                # Check if the rank's expected state matches the current state
                result = "PASS" if current in expected else "RESTART"
                status["expected"] &= current in expected

                # Restart all ranks if the expected rank is not running
                if current not in running_states:
                    status["restart"] = True
                    result = "RESTART"

                # Keep track of any hosts with a server in the errored state
                if current in errored_states:
                    if current_host not in errored_hosts:
                        errored_hosts.append(current_host)

                self.log.info(
                    log_format, rank, current_host,
                    self._expected_states[rank]["uuid"], "|".join(expected),
                    current, result)

            # Report any current states that were not expected as an error
            for rank in sorted(current_states):
                status["expected"] = False
                domain = current_states[rank]["domain"].split(".")
                self.log.info(
                    log_format, rank, domain[0].replace("/", ""),
                    current_states[rank]["uuid"], "not detected",
                    current_states[rank]["state"].lower(), "RESTART")

        elif not self._expected_states:
            # Expected states are populated as part of detect_io_server_start(),
            # so if it is empty there was an error starting the servers.
            self.log.info(
                "  Unable to obtain current server state.  Undefined expected "
                "server states due to a failure starting the servers.")
            status["restart"] = True

        else:
            # Any failure to obtain the current rank information is an error
            self.log.info(
                "  Unable to obtain current server state.  If the servers are "
                "not running this is expected.")

            # Do not report an error if all servers are expected to be stopped
            all_stopped = bool(self._expected_states)
            for rank in sorted(self._expected_states):
                states = self._expected_states[rank]["state"]
                if not isinstance(states, (list, tuple)):
                    states = [states]
                if "stopped" not in [item.lower() for item in states]:
                    all_stopped = False
                    break
            if all_stopped:
                self.log.info("  All servers are expected to be stopped.")
                status["restart"] = True
            else:
                status["expected"] = False

        # Any unexpected state detected warrants a restart of all servers
        if not status["expected"]:
            status["restart"] = True

        # Set the verified timestamp
        if set_expected and hasattr(self.manager, "timestamps"):
            self.manager.timestamps["verified"] = datetime.now().strftime(
                "%Y-%m-%d %H:%M:%S")

        # Dump the server logs for any server found in the errored state
        if errored_hosts:
            self.log.info(
                "<SERVER> logs for ranks in the errored state since start "
                "detection")
            if hasattr(self.manager, "dump_logs"):
                self.manager.dump_logs(errored_hosts)

        return status

    @fail_on(CommandFailure)
    def stop_ranks(self, ranks, daos_log, force=False):
        """Kill/Stop the specific server ranks using this pool.

        Args:
            ranks (list): a list of daos server ranks (int) to kill
            daos_log (DaosLog): object for logging messages
            force (bool, optional): whether to use --force option to dmg system
                stop. Defaults to False.

        Raises:
            avocado.core.exceptions.TestFail: if there is an issue stopping the
                server ranks.

        """
        msg = "Stopping DAOS ranks {} from server group {}".format(
            ranks, self.get_config_value("name"))
        self.log.info(msg)
        daos_log.info(msg)

        # Stop desired ranks using dmg
        self.dmg.system_stop(ranks=convert_list(value=ranks), force=force)

        # Update the expected status of the stopped/evicted ranks
        self.update_expected_states(ranks, ["stopped", "evicted"])
