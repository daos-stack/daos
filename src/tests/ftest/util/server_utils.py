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

from command_utils import (BasicParameter, FormattedParameter,
                           CommandWithParameters, CommandWithSubCommand,
                           CommandFailure, EnvironmentVariables)
from command_daos_utils import (YamlParameters, YamlCommand, SubprocessManager,
                                TransportCredentials, LogParameter)
from general_utils import pcmd
from dmg_utils import DmgCommand


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

        # All log files should be placed in the same directory on each host to
        # enable easy log file archiving by launch.py
        log_dir = os.environ.get("DAOS_TEST_LOG_DIR", "/tmp")

        self.provider = BasicParameter(None, default_provider)
        self.hyperthreads = BasicParameter(None, False)
        self.socket_dir = BasicParameter(None, "/var/run/daos_server")
        self.nr_hugepages = BasicParameter(None, 4096)
        self.control_log_mask = BasicParameter(None, "DEBUG")
        self.control_log_file = LogParameter(log_dir, None, "daos_control.log")
        self.helper_log_file = LogParameter(log_dir, None, "daos_admin.log")
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

    def set_value(self, name, value):
        """Set the value for a specified attribute name.

        Args:
            name (str): name of the attribute for which to set the value
            value (object): the value to set

        Returns:
            bool: if the attribute name was found and the value was set

        """
        status = super(DaosServerYamlParameters, self).set_value(name, value)

        # Set the value for each per-server configuration attribute name
        if not status:
            for server_params in self.server_params:
                if server_params.set_value(name, value):
                    status = True

        return status

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

    def update_log_files(self, control_log, helper_log, server_log):
        """Update the logfile parameter for the daos server.

        If there are multiple server configurations defined the server_log value
        will be made unique for each server's log_file parameter.

        Any log file name set to None will result in no update to the respective
        log file parameter value.

        Args:
            control_log (str): control log file name
            helper_log (str): helper (admin) log file name
            server_log (str): per server log file name
        """
        if control_log is not None:
            self.control_log_file.update(
                control_log, "server_config.control_log_file")
        if helper_log is not None:
            self.helper_log_file.update(
                helper_log, "server_config.helper_log_file")
        if server_log is not None:
            for index, server_params in enumerate(self.server_params):
                log_name = list(os.path.splitext(server_log))
                if len(self.server_params) > 1:
                    # Create unique log file names for each daos_io_server
                    log_name.insert(1, "_{}".format(index))
                server_params.log_file.update(
                    "".join(log_name),
                    "server_config.server[{}].log_file".format(index))

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

        def __init__(self, index=None):
            """Create a SingleServerConfig object.

            Args:
                index (int, optional): index number for the namespace path used
                    when specifying multiple servers per host. Defaults to None.
            """
            namespace = "/run/server_config/servers/*"
            if isinstance(index, int):
                namespace = "/run/server_config/servers/{}/*".format(index)
            super(
                DaosServerYamlParameters.PerServerYamlParameters,
                self).__init__(namespace)

            # Use environment variables to get default parameters
            default_interface = os.environ.get("OFI_INTERFACE", "eth0")
            default_port = int(os.environ.get("OFI_PORT", 31416))

            # All log files should be placed in the same directory on each host
            # to enable easy log file archiving by launch.py
            log_dir = os.environ.get("DAOS_TEST_LOG_DIR", "/tmp")

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
            self.log_mask = BasicParameter(None, "DEBUG")
            self.log_file = LogParameter(log_dir, None, "daos_server.log")
            self.env_vars = BasicParameter(
                None,
                ["ABT_ENV_MAX_NUM_XSTREAMS=100",
                 "ABT_MAX_NUM_XSTREAMS=100",
                 "DAOS_MD_CAP=1024",
                 "CRT_CTX_SHARE_ADDR=0",
                 "CRT_TIMEOUT=30",
                 "FI_SOCKETS_MAX_CONN_RETRY=1",
                 "FI_SOCKETS_CONN_TIMEOUT=2000",
                 "DD_MASK=mgmt,io,md,epc,rebuild"]
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
            self.scm_size = BasicParameter(None, 12)
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

            # Ignore the scm_size param when using dcpm
            if self.using_dcpm:
                self.log.debug("Ignoring the scm_size when scm_class is 'dcpm'")
                self.scm_size.update(None, "scm_size")

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
    FORMAT_PATTERN = "(SCM format required)(?!;)"

    def __init__(self, path="", yaml_cfg=None, timeout=300):
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
            self.pattern_count = host_qty
        else:
            self.pattern = self.NORMAL_PATTERN
            self.pattern_count = host_qty * len(self.yaml.server_params)

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
    """Manages the daos_server execution on one or more hosts using orterun."""

    def __init__(self, server_command, manager="OpenMPI"):
        """Initialize a DaosServerManager object.

        Args:
            server_command (ServerCommand): server command object
            manager (str, optional): the name of the JobManager class used to
                manage the YamlCommand defined through the "job" attribute.
                Defaults to "OpenMpi"
        """
        super(DaosServerManager, self).__init__(server_command, manager)
        self.manager.job.sub_command_override = "start"
        self._exe_names.append("daos_io_server")

        # Dmg command to access this group of servers which will be configured
        # to access the doas_servers when they are started
        self.dmg = DmgCommand(self.manager.job.command_path)

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
        return self.manager.job.get_interface_envs(index)

    # def _get_port_list(self, hosts):
    #     """Get a list of hosts and port numbers.

    #     Args:
    #         hosts (list): a list of hosts to join with the port number

    #     Returns:
    #         list: a list of '<host>:<port>' entries for each host

    #     """
    #     port = self.get_config_value("port")
    #     return [":".join([host, str(port)]) for host in hosts]

    # def get_host_port_list(self):
    #     """Get a list of hosts and port numbers.

    #     Returns:
    #         list: a list of '<host>:<port>' entries for each host

    #     """
    #     return self._get_port_list(self._hosts)

    # def get_access_port_list(self):
    #     """Get a list of access point hosts and port numbers.

    #     Returns:
    #         list: a list of '<host>:<port>' entries for each access point host

    #     """
    #     # For now only include the first host in the access point list
    #     hosts = self.manager.job.yaml.other_params.access_points.hosts[:1]
    #     return self._get_port_list(hosts)

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
        if self.manager.job.using_nvme:
            log_file = self.manager.job.get_config_value("log_file")
            if log_file is not None:
                self.log.info("Creating log file: %s", log_file)
                pcmd(self._hosts, "touch {}".format(log_file), False)

        # Prepare server storage
        if self.manager.job.using_nvme or self.manager.job.using_dcpm:
            self.log.info("Preparing storage in <format> mode")
            self.prepare_storage("root")
            if hasattr(self.manager, "mca"):
                self.manager.mca.update(
                    {"plm_rsh_args": "-l root"}, "orterun.mca", True)

    def start(self):
        """Start the server through the runner."""
        # Create the daos_server yaml file
        self.manager.job.create_yaml_file()

        # Update dmg command params to reflect access to all of the servers
        self.dmg.hostlist.update(",".join(self._hosts), "dmg.hostlist")
        self.dmg.insecure.update(
            self.get_config_value("allow_insecure"), "dmg.insecure")

        # Prepare the servers
        self.prepare()

        # Start the servers and wait for each host to require a SCM format
        self.log.info(
            "--- STARTING SERVERS ON %s ---",
            ", ".join([host.upper() for host in self._hosts]))
        self.manager.job.update_pattern("format", len(self._hosts))
        try:
            self.manager.run()
        except CommandFailure as error:
            self.kill()
            raise ServerFailed(
                "Failed to start servers before format: {}".format(error))

        # Format storage and wait for server to change ownership
        self.log.info("Formatting hosts: <%s>", self._hosts)
        self.dmg.storage_format()

        # Wait for all the doas_io_servers to start
        self.manager.job.update_pattern("normal", len(self._hosts))
        if not self.manager.job.check_subprocess_status(self.manager.process):
            self.kill()
            raise ServerFailed("Failed to start servers after format")

        # Update the dmg command host list to work with pool create/destroy
        self.dmg.hostlist.update(
            ",".join(self.get_config_value("access_points")), "dmg.hostlist")

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

    def clean_files(self, verbose=True):
        """Clean up the daos server files.

        Args:
            verbose (bool, optional): display clean commands. Defaults to True.
        """
        clean_cmds = []
        for server_params in self.manager.job.yaml.server_params:
            scm_mount = server_params.get_value("scm_mount")
            self.log.info("Cleaning up the %s directory.", str(scm_mount))

            # Remove the superblocks
            cmd = "rm -fr {}/*".format(scm_mount)
            if cmd not in clean_cmds:
                clean_cmds.append(cmd)

            # Dismount the scm mount point
            cmd = "while sudo umount {}; do continue; done".format(scm_mount)
            if cmd not in clean_cmds:
                clean_cmds.append(cmd)

            if self.manager.job.using_dcpm:
                scm_list = server_params.get_value("scm_list")
                if isinstance(server_params.scm_list.value, list):
                    for device in scm_list:
                        self.log.info("Cleaning up the %s device.", str(device))
                        cmd = "sudo wipefs -a {}".format(device)
                        if cmd not in clean_cmds:
                            clean_cmds.append(cmd)

        pcmd(self._hosts, "; ".join(clean_cmds), verbose)

    def prepare_storage(self, user):
        """Prepare the server storage.

        Args:
            user (str): username

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

        if self.manager.job.using_dcpm and not self.manager.job.using_nvme:
            cmd.sub_command_class.sub_command_class.scm_only.value = True
        elif not self.manager.job.using_dcpm and self.manager.job.using_nvme:
            cmd.sub_command_class.sub_command_class.nvme_only.value = True

        if self.manager.job.using_nvme:
            cmd.sub_command_class.sub_command_class.hugepages.value = 4096

        self.log.info("Preparing DAOS server storage: %s", str(cmd))
        result = pcmd(self._hosts, str(cmd), timeout=120)
        if len(result) > 1 or 0 not in result:
            dev_type = "nvme"
            if self.manager.job.using_dcpm and self.manager.job.using_nvme:
                dev_type = "dcpm & nvme"
            elif self.manager.job.using_dcpm:
                dev_type = "dcpm"
            raise ServerFailed("Error preparing {} storage".format(dev_type))

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

    def set_scm_mount_ownership(self, user=None):
        """Set the ownership to the specified user for each scm mount.

        Args:
            user (str, optional): user name. Defaults to None - current user.
        """
        user = getpass.getuser() if user is None else user
        scm_mount = self.manager.job.get_config_value("scm_mount")

        # Support single or multiple scm_mount points
        if not isinstance(scm_mount, list):
            scm_mount = [scm_mount]

        self.log.info(
            "Changing ownership to %s for: %s", user, ", ".join(scm_mount))
        cmd = "sudo chown -R {0}:{0} {1}".format(user, " ".join(scm_mount))
        pcmd(self._hosts, cmd, False)
