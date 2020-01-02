#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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
from __future__ import print_function

import os
import getpass

from command_utils import YamlParameters, BasicParameter, FormattedParameter
from command_utils import YamlCommand, SubprocessManager, CommandWithSubCommand
from command_utils import CommandWithParameters, TransportCredentials
from command_utils import CommandFailure, EnvironmentVariables
from general_utils import pcmd
from dmg_utils import storage_format

# This is out of date and should not be used
AVOCADO_FILE = "daos_avocado_test.yaml"


class ServerFailed(Exception):
    """Server didn't start/stop properly."""


class DaosServerTransportCredentials(TransportCredentials):
    """Transport credentials listing certificates for secure communication."""

    def __init__(self):
        """Initialize a TransportConfig object."""
        super(DaosServerTransportCredentials, self).__init__(
            "/run/server_config/transport_config/*", "transport_config")

        # Additional daos_server transport credential parameters:
        #   - client_cert_dir: <str>, e.g. "".daos/clients"
        #       Location of client certificates [daos_server only]
        #
        self.client_cert_dir = BasicParameter(None)


class DaosServerYamlParameters(YamlParameters):
    """Defines the daos_server configuration yaml parameters."""

    def __init__(self, filename, common_yaml):
        """Initialize an DaosServerYamlParameters object.

        Args:
            filename (str): yaml configuration file name
            common_yaml (YamlParameters): [description]
        """
        super(DaosServerYamlParameters, self).__init__(
            "/run/server_config/*", filename, None, common_yaml)

        # daos_server configuration file parameters
        #
        #   - provider: <str>, e.g. ofi+verbs;ofi_rxm
        #       Force a specific provider to be used by all the servers.
        #
        #   - hyperthreads: <bool>, e.g. True
        #       When Hyperthreading is enabled and supported on the system, this
        #       parameter defines whether the DAOS service thread should only be
        #       bound to different physical cores (False) or hyperthreads (True)
        #
        #   - socket_dir: <str>, e.g. /var/run/daos_server
        #       DAOS Agent and DAOS Server both use unix domain sockets for
        #       communication with other system components. This setting is the
        #       base location to place the sockets in.
        #
        #   - nr_hugepages: <int>, e.g. 4096
        #       Number of hugepages to allocate for use by NVMe SSDs
        #
        #   - control_log_mask: <str>, e.g. DEBUG
        #       Force specific debug mask for daos_server (control plane).
        #
        #   - control_log_file: <str>, e.g. /tmp/daos_control.log
        #       Force specific path for daos_server (control plane) logs.
        #
        #   - user_name: <str>, e.g. daosuser
        #       Username used to lookup user uid/gid to drop privileges to if
        #       started as root. After control plane start-up and configuration,
        #       before starting data plane, process ownership will be dropped to
        #       those of supplied user.
        #
        #   - group_name: <str>, e.g. daosgroup
        #       Group name used to lookup group gid to drop privileges to when
        #       user_name is root. If user is a member of group, this group gid
        #       is set for the running process. If group look up fails or user
        #       is not member, use uid return from user lookup.
        #
        default_provider = os.environ.get("CRT_PHY_ADDR_STR", "ofi+sockets")

        self.provider = BasicParameter(None, default_provider)
        self.hyperthreads = BasicParameter(None, False)
        self.socket_dir = BasicParameter(None, "/var/run/daos_server")
        self.nr_hugepages = BasicParameter(None, 4096)
        self.control_log_mask = BasicParameter(None, "DEBUG")
        self.control_log_file = BasicParameter(None, "/tmp/daos_control.log")
        self.helper_log_file = BasicParameter(None, "/tmp/daos_admin.log")
        self.user_name = BasicParameter(None)
        self.group_name = BasicParameter(None)

        # Single server config parameters
        self.server_params = [self.PerServerYamlParameters()]

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        If no key matches are found in the yaml file the BasicParameter object
        will be set to its default value.

        Args:
            test (Test): avocado Test object
        """
        super(DaosServerYamlParameters, self).get_params(test)
        for server_params in self.server_params:
            server_params.get_params(test)

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        # Get the common config yaml parameters
        yaml_data = super(DaosServerYamlParameters, self).get_yaml_data()

        # Add the per-server yaml parameters
        yaml_data["servers"] = []
        for index in range(len(self.server_params)):
            yaml_data["servers"].append({})
            for name in self.server_params[index].get_param_names():
                value = getattr(self.server_params[index], name).value
                if value is not None and value is not False:
                    yaml_data["servers"][index][name] = value

        return yaml_data

    def get_value(self, name):
        """Get the value of the specified attribute name.

        Args:
            name (str): name of the attribute from which to get the value

        Returns:
            object: the object's value referenced by the attribute name

        """
        value = super(DaosServerYamlParameters, self).get_value(name)

        # Look for the value in the per-server configuration parameters.  The
        # first value found will be returned.
        index = 0
        while value is None and index < len(self.server_params):
            value = self.server_params[index].get_value(name)
            index += 1

        return value

    @property
    def using_nvme(self):
        """Is the configuration file setup to use NVMe devices.

        Returns:
            bool: True if NVMe devices are configured for at least one server in
                the config file; False otherwise

        """
        for server_params in self.server_params:
            if server_params.using_nvme:
                return True
        return False

    @property
    def using_dcpm(self):
        """Is the configuration file setup to use SCM devices.

        Returns:
            bool: True if SCM devices are configured for at least one server in
                the config file; False otherwise

        """
        for server_params in self.server_params:
            if server_params.using_dcpm:
                return True
        return False

    def update_control_log_file(self, name):
        """Update the daos server control log file parameter.

        Args:
            name (str): new log file name
        """
        if name is not None:
            self.control_log_file.update(name, "control_log_file")

    def update_server_log_files(self, names):
        """Update the daos server log file parameter.

        Args:
            names (list|str): list of new server log file names.  A single
                string is also supported, where if needed, the string will be
                used to auto-generate a unique log file for each individual
                server parameter section.
        """
        # Support specifying a single log file name as a template
        if not isinstance(names, list):
            if len(self.server_params) == 1:
                # If there is only one set of server parameters, use the string
                names = [names]
            else:
                # For multiple server parameters create a list of unique log
                # file names for each set of server parameters, e.g.
                #   Convert '/path/to/file/log_file_name.log' into:
                #       /path/to/file/log_file_name_0.log,
                #       /path/to/file/log_file_name_1.log,
                #       etc.
                file_split = list(os.path.splitext(names))
                names = [
                    "".join(file_split.copy().insert("_{}".format(index)))
                    for index, _ in enumerate(self.server_params)]

        # Assign the log file names for each set of server parameters
        for index, name in enumerate(names):
            if name is not None and index < len(self.server_params):
                self.server_params[index].update_log_file(name)

    def get_interface_envs(self, index=0):
        """Get the environment variable names and values for the interfaces.

        Args:
            index (int, optional): server index from which to obtain the
                environment variable values. Defaults to 0.

        Returns:
            EnvironmentVariables: a dictionary of environment variable names
                and their values extracted from the daos_server yaml
                configuration file.

        """
        env = EnvironmentVariables()
        mapping = {
            "OFI_INTERFACE": "fabric_iface",
            "OFI_PORT": "fabric_iface_port",
            "CRT_PHY_ADDR_STR": "provider",
        }
        for key, name in mapping.items():
            value = self.server_params[index].get_value(name)
            if value is not None:
                env[key] = value

        return env

    class PerServerYamlParameters(YamlParameters):
        """Defines the configuration yaml parameters for a single server."""

        def __init__(self):
            """Create a SingleServerConfig object."""
            super(
                DaosServerYamlParameters.PerServerYamlParameters,
                self).__init__("/run/server_config/servers/*")

            # Use environment variables to get default parameters
            default_interface = os.environ.get("OFI_INTERFACE", "eth0")
            default_port = int(os.environ.get("OFI_PORT", 31416))

            # Parameters
            #   targets:                count of VOS targets
            #   first_core:             starting index for targets
            #   nr_xs_helpers:          offload helpers per target
            #   fabric_iface:           map to OFI_INTERFACE=eth0
            #   fabric_iface_port:      map to OFI_PORT=31416
            #   log_mask:               map to D_LOG_MASK env
            #   log_file:               map to D_LOG_FILE env
            #   env_vars:               influences DAOS IO Server behaviour
            #       Add to enable scalable endpoint:
            #           - CRT_CREDIT_EP_CTX=0
            #           - CRT_CTX_SHARE_ADDR=1
            #           - CRT_CTX_NUM=8
            #       nvme options:
            #           - IO_STAT_PERIOD=10
            self.targets = BasicParameter(None, 8)
            self.first_core = BasicParameter(None, 0)
            self.nr_xs_helpers = BasicParameter(None, 2)
            self.fabric_iface = BasicParameter(None, default_interface)
            self.fabric_iface_port = BasicParameter(None, default_port)
            self.log_mask = BasicParameter(None, "DEBUG,RPC=ERR,MEM=ERR")
            self.log_file = BasicParameter(None, "/tmp/server.log")
            self.env_vars = BasicParameter(
                None,
                ["ABT_ENV_MAX_NUM_XSTREAMS=100",
                 "ABT_MAX_NUM_XSTREAMS=100",
                 "DAOS_MD_CAP=1024",
                 "CRT_CTX_SHARE_ADDR=0",
                 "CRT_TIMEOUT=30",
                 "FI_SOCKETS_MAX_CONN_RETRY=1",
                 "FI_SOCKETS_CONN_TIMEOUT=2000"]
            )

            # Storage definition parameters:
            #
            # When scm_class is set to ram, tmpfs will be used to emulate SCM.
            #   scm_mount: /mnt/daos        - map to -s /mnt/daos
            #   scm_class: ram
            #   scm_size: 6                 - size in GB units
            #
            # When scm_class is set to dcpm, scm_list is the list of device
            # paths for AppDirect pmem namespaces (currently only one per
            # server supported).
            #   scm_class: dcpm
            #   scm_list: [/dev/pmem0]
            #
            # If using NVMe SSD (will write /mnt/daos/daos_nvme.conf and start
            # I/O service with -n <path>)
            #   bdev_class: nvme
            #   bdev_list: ["0000:81:00.0"] - generate regular nvme.conf
            #
            # If emulating NVMe SSD with malloc devices
            #   bdev_class: malloc          - map to VOS_BDEV_CLASS=MALLOC
            #   bdev_size: 4                - malloc size of each device in GB.
            #   bdev_number: 1              - generate nvme.conf as follows:
            #       [Malloc]
            #       NumberOfLuns 1
            #       LunSizeInMB 4000
            #
            # If emulating NVMe SSD over kernel block device
            #   bdev_class: kdev            - map to VOS_BDEV_CLASS=AIO
            #   bdev_list: [/dev/sdc]       - generate nvme.conf as follows:
            #       [AIO]
            #       AIO /dev/sdc AIO2
            #
            # If emulating NVMe SSD with backend file
            #   bdev_class: file            - map to VOS_BDEV_CLASS=AIO
            #   bdev_size: 16               - file size in GB. Create file if
            #                                 it does not exist.
            #   bdev_list: [/tmp/daos-bdev] - generate nvme.conf as follows:
            #       [AIO]
            #       AIO /tmp/aiofile AIO1 4096
            self.scm_mount = BasicParameter(None, "/mnt/daos")
            self.scm_class = BasicParameter(None, "ram")
            self.scm_size = BasicParameter(None, 6)
            self.scm_list = BasicParameter(None)
            self.bdev_class = BasicParameter(None)
            self.bdev_list = BasicParameter(None)
            self.bdev_size = BasicParameter(None)
            self.bdev_number = BasicParameter(None)

        def get_params(self, test):
            """Get values for the daos server yaml config file.

            Args:
                test (Test): avocado Test object
            """
            super(
                DaosServerYamlParameters.PerServerYamlParameters,
                self).get_params(test)

            # Override the log file file name with the test log file name
            if hasattr(test, "server_log") and test.server_log is not None:
                self.log_file.value = test.server_log

        @property
        def using_nvme(self):
            """Is the configuration file setup to use NVMe devices.

            Returns:
                bool: True if NVMe devices are configured; False otherwise

            """
            return self.bdev_class.value == "nvme"

        @property
        def using_dcpm(self):
            """Is the configuration file setup to use SCM devices.

            Returns:
                bool: True if SCM devices are configured; False otherwise

            """
            return self.scm_class.value == "dcpm"

        def update_log_file(self, name):
            """Update the daos server log file parameter.

            Args:
                name (str): new log file name
            """
            self.log_file.update(name, "log_file")


class DaosServerCommand(YamlCommand):
    """Defines an object representing the daos_server command."""

    NORMAL_PATTERN = "DAOS I/O server.*started"
    FORMAT_PATTERN = "SCM format required"

    def __init__(self, path="", yaml_cfg=None, timeout=120):
        """Create a daos_server command object.

        Args:
            path (str): path to location of daos_server binary.
            yaml_cfg (YamlParameters, optional): yaml configuration parameters.
                Defaults to None.
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 120 seconds.
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
        # -j, --json         Enable JSON output
        # -o, --config-path= Path to agent configuration file
        self.debug = FormattedParameter("--debug", True)
        self.json = FormattedParameter("--json", False)
        self.config = FormattedParameter("--config={}", default_yaml_file)

        # Additional daos_server command line parameters:
        #     --allow-proxy  Allow proxy configuration via environment
        self.allow_proxy = FormattedParameter("--allow-proxy", False)

        # Used to override the sub_command.value parameter value
        self.sub_command_override = None

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
        self.update_pattern()

    def update_pattern(self, override=None):
        """Update the pattern used to determine if the daos_server started.

        Args:
            override (str, optional): operation mode used to override the mode
                determined from the yaml parameter values. Defaults to None -
                no override.
        """
        # Determine the mode from either the override (if specified) or the yaml
        # parameter values (if no override specified)
        mode = "normal"
        if isinstance(override, str):
            mode = override.lower()
        elif self.using_nvme or self.using_dcpm:
            mode = "format"

        if mode == "format":
            self.pattern = self.FORMAT_PATTERN
        else:
            self.pattern = self.NORMAL_PATTERN

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

    @property
    def mode(self):
        """Get the current operation mode.

        Returns:
            str: current mode

        """
        mode = "normal"
        if self.using_dcpm or self.using_nvme:
            mode = "format"
        return mode

    def get_interface_envs(self, index=0):
        """Get the environment variable names and values for the interfaces.

        Args:
            index (int, optional): server index from which to obtain the
                environment variable values. Defaults to 0.

        Returns:
            EnvironmentVariables: a dictionary of environment variable names
                and their values extracted from the daos_server yaml
                configuration file.

        """
        return self.yaml.get_interface_envs(index)

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
            #                           daos_io_server sockets
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
            self.recreate = FormattedParameter("--recreate-superblocks", True)

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
    """Manages the daos_server execution on one or more hosts using orterun."""

    def __init__(self, ompi_path, server_command):
        """Initialize a DaosServerManager object.

        Args:
            ompi_path (str): path to location of orterun binary.
            server_command (ServerCommand): server command object
        """
        super(DaosServerManager, self).__init__(
            "/run/server_config", server_command, ompi_path)
        self.job.sub_command_override = "start"
        self._exe_names.append("daos_io_server")

    def get_interface_envs(self, index=0):
        """Get the environment variable names and values for the interfaces.

        Args:
            index (int, optional): server index from which to obtain the
                environment variable values. Defaults to 0.

        Returns:
            EnvironmentVariables: a dictionary of environment variable names
                and their values extracted from the daos_server yaml
                configuration file.

        """
        return self.job.get_interface_envs(index)

    def prepare(self):
        """Prepare the host to run the server."""
        self.log.info(
            "--- PREPARING SERVERS ON %s ---",
            ", ".join([host.upper() for host in self._hosts]))

        # Kill any doas servers running on the hosts
        self.kill()

        # Clean up any files that exist on the hosts
        self.clean_files()

        # Make sure log file has been created for ownership change
        if self.job.using_nvme:
            log_file = self.job.get_config_value("log_file")
            if log_file is not None:
                self.log.info("Creating log file: %s", log_file)
                pcmd(self._hosts, "touch {}".format(log_file), False)

        # Prepare server storage
        if self.job.using_nvme or self.job.using_dcpm:
            self.log.info("Preparing storage in <format> mode")
            self.prepare_storage("root")
            self.mca.value = {"plm_rsh_args": "-l root"}

    def start(self):
        """Start the server through the runner."""
        # Prepare the servers
        self.prepare()

        # Start the servers
        self.log.info(
            "--- STARTING SERVERS ON %s ---",
            ", ".join([host.upper() for host in self._hosts]))
        try:
            self.run()
        except CommandFailure as details:
            self.log.info("<SERVER> Exception occurred: %s", str(details))
            # Kill the subprocess, anything that might have started
            self.kill()
            raise ServerFailed(
                "Failed to start server in {} mode.".format(self.job.mode))

        if self.job.using_nvme or self.job.using_dcpm:
            # Setup the hostlist to pass to dmg command
            port = self.job.get_config_value("port")
            ported_hosts = ["{}:{}".format(host, port) for host in self._hosts]

            # Format storage and wait for server to change ownership
            self.log.info("Formatting hosts: <%s>", self._hosts)
            storage_format(self.job.path, ",".join(ported_hosts))
            self.job.update_pattern("normal")
            try:
                self.job.check_subprocess_status(self.process)
            except CommandFailure as error:
                self.log.info("Failed to start after format: %s", str(error))

        return True

    def stop(self):
        """Stop the server through the runner."""
        self.log.info("Stopping server orterun command")

        # Maintain a running list of errors detected trying to stop
        messages = []

        # Stop the subprocess running the orterun command
        try:
            super(DaosServerManager, self).stop()
        except CommandFailure as error:
            messages.append(
                "Error stopping the orterun subprocess: {}".format(error))

        # Kill any leftover processes that may not have been stopped correctly
        self.kill()

        if self.job.using_nvme:
            # Reset the storage
            try:
                self.reset_storage()
            except ServerFailed as error:
                messages.append(str(error))

            # Make sure the mount directory belongs to non-root user
            self.set_scm_mount_ownership()

        # Report any errors after all stop actions have been attempted
        if len(messages) > 0:
            raise ServerFailed(
                "Failed to stop servers:\n  {}".format("\n  ".join(messages)))

    def clean_files(self, verbose=True):
        """Clean up the daos server files.

        Args:
            verbose (bool, optional): display clean commands. Defaults to True.
        """
        scm_list = self.job.get_config_value("scm_list")
        scm_mount = self.job.get_config_value("scm_mount")

        # Support single or multiple scm_mount points
        if not isinstance(scm_mount, list):
            scm_mount = [scm_mount]

        # Set up the list of commands used to clean up after the daos server
        clean_cmds = []
        for mount in scm_mount:
            clean_cmds.append(
                "find {} -mindepth 1 -maxdepth 1 -print0 | "
                "xargs -0r rm -rf".format(mount))

        # # Add wiping all files in SCM mount points if using NVMe
        # if self.job.using_nvme:
        #     for mount in scm_mount:
        #         clean_cmds.append("sudo rm -rf {}".format(mount))

        # Add unmounting all SCM mount points if using SCM or NVMe
        if self.job.using_nvme or self.job.using_dcpm:
            for mount in scm_mount:
                clean_cmds.append("sudo umount {}".format(mount))

        # Add wiping the filesystem from each SCM device if using SCM
        if self.job.using_dcpm:
            for value in scm_list:
                clean_cmds.append("sudo wipefs -a {}".format(value))

        self.log.info("Cleaning up directories: %s", str(scm_mount))
        pcmd(self._hosts, "; ".join(clean_cmds), verbose)

    def prepare_storage(self, user):
        """Prepare the server storage.

        Args:
            user (str): username

        Raises:
            ServerFailed: if there was an error preparing the storage.

        """
        cmd = DaosServerCommand(self.job.path)
        cmd.sudo = True
        cmd.debug.value = False
        cmd.set_sub_command("storage")
        cmd.sub_command_class.set_sub_command("prepare")
        cmd.sub_command_class.sub_command_class.target_user.value = user
        cmd.sub_command_class.sub_command_class.force.value = True

        if self.job.using_dcpm and not self.job.using_nvme:
            cmd.sub_command_class.sub_command_class.scm_only.value = True
        elif not self.job.using_dcpm and self.job.using_nvme:
            cmd.sub_command_class.sub_command_class.nvme_only.value = True

        if self.job.using_nvme:
            cmd.sub_command_class.sub_command_class.hugepages.value = 4096

        self.log.info("Preparing DAOS server storage: %s", str(cmd))
        result = pcmd(self._hosts, str(cmd), timeout=120)
        if len(result) > 1 or 0 not in result:
            raise ServerFailed("Error preparing NVMe storage")

    def reset_storage(self):
        """Reset the server storage."""
        cmd = DaosServerCommand(self.job.path)
        cmd.sudo = True
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

    def set_scm_mount_ownership(self, user=None):
        """Set the ownership to the specified user for each scm mount.

        Args:
            user (str, optional): user name. Defaults to None - current user.
        """
        user = getpass.getuser() if user is None else user
        scm_mount = self.job.get_config_value("scm_mount")

        # Support single or multiple scm_mount points
        if not isinstance(scm_mount, list):
            scm_mount = [scm_mount]

        self.log.info(
            "Changing ownership to %s for: %s", user, ", ".join(scm_mount))
        cmd = "sudo chown -R {0}:{0} {1}".format(user, " ".join(scm_mount))
        pcmd(self._hosts, cmd, False)
