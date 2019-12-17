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

import traceback
import sys
import os
import re
import time
import yaml
import getpass

from command_utils import YamlParameters, BasicParameter, FormattedParameter
from command_utils import YamlCommand, SubprocessManager, CommandWithSubCommand
from command_utils import CommandWithParameters, TransportCredentials
from command_utils import CommandFailure
from general_utils import pcmd
from dmg_utils import storage_format
from write_host_file import write_host_file


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
            names (str): list of new server log file names
        """
        for index, name in enumerate(names):
            if name is not None and index < len(self.server_params):
                self.server_params[index].update_log_file(name)

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

    # NORMAL_PATTERN = "DAOS I/O server.*started"
    NORMAL_PATTERN = "instance ready: uri:.* drpcListenerSock:"
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

    def update_log_files(self, control_log, server_logs):
        """Update the daos server log file parameters.

        Args:
            control_log (str): new control log file name
            server_logs (list): list of new server log file names
        """
        if isinstance(self.yaml, YamlParameters):
            self.yaml.update_control_log_file(control_log)
            self.yaml.update_server_log_files(server_logs)

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

    def start(self):
        """Start the server through the runner."""
        self.log.info(
            "--- PREPARING SERVERS ON %s ---",
            ", ".join([host.upper() for host in self._hosts]))

        self.server_clean()

        # Prepare SCM storage in servers
        storage_prep_flag = ""
        if self.job.using_dcpm:
            storage_prep_flag = "dcpm"
            self.log.info("Performing SCM storage prepare in <format> mode")
        else:
            storage_prep_flag = "ram"

        # Prepare nvme storage in servers
        if self.job.using_nvme:
            if storage_prep_flag == "dcpm":
                storage_prep_flag = "dcpm_nvme"
            elif storage_prep_flag == "ram":
                storage_prep_flag = "ram_nvme"
            else:
                storage_prep_flag = "nvme"
            self.log.info("Performing NVMe storage prepare in <format> mode")
            # Make sure log file has been created for ownership change
            log_file = self.job.get_config_value("log_file")
            if log_file is not None:
                self.log.info("Creating log file: %s", log_file)
                pcmd(self._hosts, "touch {}".format(log_file), False)

        if storage_prep_flag != "ram":
            self.prepare_storage("root", storage_prep_flag)
            self.mca.value = {"plm_rsh_args": "-l root"}

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

    def server_clean(self):
        """Prepare the hosts before starting daos server."""
        # Kill any doas servers running on the hosts
        self.kill()
        # Clean up any files that exist on the hosts
        self.clean_files()

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

    def prepare_storage(self, user, device_type):
        """Prepare the server storage.

        Args:
            user (str): username
            device_type (str): storage device type

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

        if device_type == "dcpm":
            cmd.sub_command_class.sub_command_class.scm_only.value = True
        elif device_type == "dcpm_nvme":
            cmd.sub_command_class.sub_command_class.hugepages.value = 4096
        elif device_type == "ram_nvme" or device_type == "nvme":
            cmd.sub_command_class.sub_command_class.nvme_only.value = True
            cmd.sub_command_class.sub_command_class.hugepages.value = 4096
        else:
            raise ServerFailed("Invalid device type")

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

    # def prepare(self):
    #     """Prepare the storge before starting the daos_server."""
    #     self.log.info(
    #         "--- PREPARING SERVERS ON %s ---",
    #         ", ".join([host.upper() for host in self._hosts]))

    #     # Kill any doas servers running on the hosts
    #     self.kill()

    #     # Clean up any files that exist on the hosts
    #     self.clean_files()

    #     # Ensure the server log file can be accessed by both this user and root
    #     if self.job.using_dcpm or self.job.using_nvme:
    #         log_file = self.job.get_config_value("log_file")
    #         if log_file is not None:
    #             self.log.info("Creating log file: %s", log_file)
    #             pcmd(self._hosts, "touch {}".format(log_file), False)

    #     # Prepare SCM/NVMe storage if present
    #     self.prepare_storage("root")

    #     # Issue the orterun command as root with SCM or NVMe storage
    #     if self.job.using_dcpm or self.job.using_nvme:
    #         self.runner.mca.value = {"plm_rsh_args": "-l root"}

    # def prepare_storage(self, user):
    #     """Prepare the server storage.

    #     Args:
    #         user (str): username

    #     Raises:
    #         ServerFailed: if there was an error preparing the storage.

    #     """
    #     if self.job.using_dcpm or self.job.using_nvme:
    #         cmd = DaosServerCommand(self.job.path)
    #         cmd.sudo = True
    #         cmd.set_sub_command("storage")
    #         cmd.sub_command_class.set_sub_command("prepare")
    #         cmd.sub_command_class.sub_command_class.target_user.value = user
    #         cmd.sub_command_class.sub_command_class.force.value = True

    #         prepare_type = "SCM and NVMe"
    #         if self.job.using_dcpm and not self.job.using_nvme:
    #             prepare_type = "SCM"
    #             cmd.sub_command_class.sub_command_class.scm_only.value = True
    #         elif not self.job.using_dcpm and self.job.using_nvme:
    #             prepare_type = "NVMe"
    #             cmd.sub_command_class.sub_command_class.nvme_only.value = True

    #         if self.job.using_nvme:
    #             cmd.sub_command_class.sub_command_class.hugepages.value = 4096

    #         self.log.info(
    #             "Preparing DAOS %s server storage: %s", prepare_type, str(cmd))
    #         result = pcmd(self._hosts, str(cmd), timeout=120)
    #         if len(result) > 1 or 0 not in result:
    #             raise ServerFailed("Error preparing storage")

# def storage_prepare(hosts, user, device_type, bin_path):
#     """Prepare the server storage using the DAOS server's yaml settings file.

#     Args:
#         hosts (str): a string of comma-separated host names
#         user (): Username
#         device_type (): scm or nvme
#         bin_path ():

#     Raises:
#         ServerFailed: if server failed to prepare storage
#     """
#     # Get the daos_server from the install path. Useful for testing
#     # with daos built binaries.
#     dev_param = ""
#     device_args = ""
#     if device_type == "dcpm":
#         dev_param = "-s"
#     elif device_type == "dcpm_nvme":
#         device_args = " --hugepages=4096"
#     elif device_type == "ram_nvme" or device_type == "nvme":
#         dev_param = "-n"
#         device_args = " --hugepages=4096"
#     else:
#         raise ServerFailed("Invalid device type")
#     cmd = ("sudo {} storage prepare {} -u \"{}\" {} -f"
#            .format(, dev_param, user, device_args))
#     result = pcmd(hosts, cmd, timeout=120)
#     if len(result) > 1 or 0 not in result:
#         raise ServerFailed("Error preparing NVMe storage")


# def storage_reset(hosts):
#     """
#     Reset the Storage on servers using the DAOS server's yaml settings file.
#     NOTE: Don't enhance this method to reset SCM. SCM
#     will not be in a useful state for running next tests.
#     Args:
#         hosts (str): a string of comma-separated host names
#     Raises:
#         ServerFailed: if server failed to reset storage
#     """
#     daos_srv_bin = get_file_path("bin/daos_server")
#     cmd = "sudo {} storage prepare -n --reset -f".format(daos_srv_bin[0])
#     result = pcmd(hosts, cmd)
#     if len(result) > 1 or 0 not in result:
#         raise ServerFailed("Error resetting NVMe storage")


# def run_server(test, hostfile, setname, uri_path=None, env_dict=None,
#                clean=True):
#     """Launch DAOS servers in accordance with the supplied hostfile.

#     Args:
#         test (Test): avocado Test object
#         hostfile (str): hostfile defining on which hosts to start servers
#         setname (str): session name
#         uri_path (str, optional): path to uri file. Defaults to None.
#         env_dict (dict, optional): dictionary on env variable names and values.
#             Defaults to None.
#         clean (bool, optional): clean the mount point. Defaults to True.

#     Raises:
#         ServerFailed: if there is an error starting the servers

#     """
#     global SESSIONS    # pylint: disable=global-variable-not-assigned
#     try:
#         servers = (
#             [line.split(' ')[0] for line in genio.read_all_lines(hostfile)])
#         server_count = len(servers)

#         # Pile of build time variables
#         with open("../../.build_vars.json") as json_vars:
#             build_vars = json.load(json_vars)

#         # Create the DAOS server configuration yaml file to pass
#         # with daos_server -o <FILE_NAME>
#         print("Creating the server yaml file in {}".format(test.tmp))
#         server_yaml = os.path.join(test.tmp, AVOCADO_FILE)
#         server_config = DaosServerConfig()
#         server_config.get_params(test)
#         access_points = ":".join((servers[0], str(server_config.port)))
#         server_config.access_points.value = access_points.split()
#         # if hasattr(test, "server_log") and test.server_log is not None:
#         #     server_config.update_log_file(test.server_log)
#         server_config.create_yaml(server_yaml)

#         # first make sure there are no existing servers running
#         print("Removing any existing server processes")
#         kill_server(servers)

#         # clean the tmpfs on the servers
#         if clean:
#             print("Cleaning the server tmpfs directories")
#             result = pcmd(
#                 servers,
#                 "find /mnt/daos -mindepth 1 -maxdepth 1 -print0 | "
#                 "xargs -0r rm -rf",
#                 verbose=False)
#             if len(result) > 1 or 0 not in result:
#                 raise ServerFailed(
#                     "Error cleaning tmpfs on servers: {}".format(
#                         ", ".join(
#                             [str(result[key]) for key in result if key != 0])))

#         server_cmd = [
#             os.path.join(build_vars["OMPI_PREFIX"], "bin", "orterun"),
#             "--np", str(server_count)]
#         if uri_path is not None:
#             server_cmd.extend(["--report-uri", uri_path])
#         server_cmd.extend(["--hostfile", hostfile, "--enable-recovery"])

#         # Add any user supplied environment
#         if env_dict is not None:
#             for key, value in env_dict.items():
#                 os.environ[key] = value
#                 server_cmd.extend(["-x", "{}={}".format(key, value)])

#         # the remote orte needs to know where to find daos, in the
#         # case that it's not in the system prefix
#         # but it should already be in our PATH, so just pass our
#         # PATH along to the remote
#         if build_vars["PREFIX"] != "/usr":
#             server_cmd.extend(["-x", "PATH"])

#         # Run server in insecure mode until Certificate tests are in place
#         server_cmd.extend(
#             [os.path.join(build_vars["PREFIX"], "bin", "daos_server"),
#              "--debug",
#              "--config", server_yaml,
#              "start", "-i", "--recreate-superblocks"])

#         print("Start CMD>>>>{0}".format(' '.join(server_cmd)))

#         resource.setrlimit(
#             resource.RLIMIT_CORE,
#             (resource.RLIM_INFINITY, resource.RLIM_INFINITY))

#         SESSIONS[setname] = subprocess.Popen(server_cmd,
#                                              stdout=subprocess.PIPE,
#                                              stderr=subprocess.PIPE)
#         fdesc = SESSIONS[setname].stdout.fileno()
#         fstat = fcntl.fcntl(fdesc, fcntl.F_GETFL)
#         fcntl.fcntl(fdesc, fcntl.F_SETFL, fstat | os.O_NONBLOCK)
#         timeout = 600
#         start_time = time.time()
#         matches = 0
#         pattern = "DAOS I/O server.*started"
#         expected_data = "Starting Servers\n"
#         while True:
#             output = ""
#             try:
#                 output = SESSIONS[setname].stdout.read()
#             except IOError as excpn:
#                 if excpn.errno != errno.EAGAIN:
#                     raise ServerFailed("Server didn't start: {}".format(excpn))
#                 continue
#             match = re.findall(pattern, output)
#             expected_data += output
#             matches += len(match)
#             if not output or matches == server_count or \
#                time.time() - start_time > timeout:
#                 print("<SERVER>: {}".format(expected_data))
#                 if matches != server_count:
#                     raise ServerFailed("Server didn't start!")
#                 break
#         print(
#             "<SERVER> server started and took {} seconds to start".format(
#                 time.time() - start_time))

#     except Exception as error:
#         print("<SERVER> Exception occurred: {0}".format(str(error)))
#         traceback.print_exception(error.__class__, error, sys.exc_info()[2])
#         # We need to end the session now -- exit the shell
#         try:
#             SESSIONS[setname].send_signal(signal.SIGINT)
#             time.sleep(5)
#             # get the stderr
#             error = SESSIONS[setname].stderr.read()
#             if SESSIONS[setname].poll() is None:
#                 SESSIONS[setname].kill()
#             retcode = SESSIONS[setname].wait()
#             print(
#                 "<SERVER> server start return code: {}\nstderr:\n{}".format(
#                     retcode, error))
#         except KeyError:
#             pass
#         raise ServerFailed("Server didn't start!")


# def stop_server(setname=None, hosts=None):
#     """Stop the daos servers.

#     Attempt to initiate an orderly shutdown of all orterun processes it has
#     spawned by sending a ctrl-c to the process matching the setname (or all
#     processes if no setname is provided).

#     If a list of hosts is provided, verify that all daos server processes are
#     dead.  Report an error if any processes are found and attempt to forcably
#     kill the processes.

#     Args:
#         setname (str, optional): server group name used to match the session
#             used to start the server. Defaults to None.
#         hosts (list, optional): list of hosts running the server processes.
#             Defaults to None.

#     Raises:
#         ServerFailed: if there was an error attempting to send a signal to stop
#             the processes running the servers or after sending the signal if
#             there are processes stiull running.

#     """
#     global SESSIONS    # pylint: disable=global-variable-not-assigned
#     try:
#         if setname is None:
#             for _key, val in SESSIONS.items():
#                 val.send_signal(signal.SIGINT)
#                 time.sleep(5)
#                 if val.poll() is None:
#                     val.kill()
#                 val.wait()
#         else:
#             SESSIONS[setname].send_signal(signal.SIGINT)
#             time.sleep(5)
#             if SESSIONS[setname].poll() is None:
#                 SESSIONS[setname].kill()
#             SESSIONS[setname].wait()
#         print("<SERVER> server stopped")

#     except Exception as error:
#         print("<SERVER> Exception occurred: {0}".format(str(error)))
#         raise ServerFailed("Server didn't stop!")

#     if not hosts:
#         return

#     # Make sure the servers actually stopped.  Give them time to stop first
#     # pgrep exit status:
#     #   0 - One or more processes matched the criteria.
#     #   1 - No processes matched.
#     #   2 - Syntax error in the command line.
#     #   3 - Fatal error: out of memory etc.
#     time.sleep(5)
#     result = pcmd(
#         hosts, "pgrep '(daos_server|daos_io_server)'", False, expect_rc=1)
#     if len(result) > 1 or 1 not in result:
#         bad_hosts = [
#             node for key in result if key != 1 for node in list(result[key])]
#         kill_server(bad_hosts)
#         raise ServerFailed(
#             "DAOS server processes detected after attempted stop on {}".format(
#                 ", ".join([str(result[key]) for key in result if key != 1])))

#     # we can also have orphaned ssh processes that started an orted on a
#     # remote node but never get cleaned up when that remote node spontaneiously
#     # reboots
#     subprocess.call(["pkill", "^ssh$"])


# def kill_server(hosts):
#     """Forcably kill any daos server processes running on the specified hosts.

#     Sometimes stop doesn't get everything.  Really whack everything with this.

#     Args:
#         hosts (list): list of host names where servers are running
#     """
#     kill_cmds = [
#         "pkill '(daos_server|daos_io_server)' --signal INT",
#         "sleep 5",
#         "pkill '(daos_server|daos_io_server)' --signal KILL",
#     ]
#     # Intentionally ignoring the exit status of the command
#     pcmd(hosts, "; ".join(kill_cmds), False, None, None)
