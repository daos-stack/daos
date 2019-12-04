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
from logging import getLogger
import os

from command_utils import BasicParameter, YamlParameters, CommandWithParameters
from command_utils import DaosYamlCommand, SubprocessManager, FormattedParameter
from general_utils import pcmd


def stop_server_processes(hosts):
    """Stop the daos server processes on the specified list of hosts.

    Args:
        hosts (list): hosts on which to stop the daos server processes
    """
    log = getLogger()
    log.info("Killing any server processes on %s", hosts)
    if hosts is not None:
        pattern = "'(daos_server|daos_io_server)'"
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


class ServerYamlParameters(YamlParameters):
    """Defines the daos_server configuration yaml parameters."""

    def __init__(self, filename, common_yaml):
        """Initialize an ServerYamlParameters object.

        Args:
            filename (str): yaml configuration file name
            common_yaml (YamlParameters): [description]
        """
        super(ServerYamlParameters, self).__init__(
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
        self.provider = BasicParameter(None, "ofi+sockets")
        # self.hyperthreads = BasicParameter(None, True)
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
        super(ServerYamlParameters, self).get_params(test)
        for server_params in self.server_params:
            server_params.get_params(test)

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        # Get the common config yaml parameters
        yaml_data = super(ServerYamlParameters, self).get_yaml_data()

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
        value = super(ServerYamlParameters, self).get_value(name)

        # Look for the value in the per-server configuration parameters.  The
        # first value found will be returned.
        index = 0
        while value is None and index < len(self.server_params):
            value = self.server_params[index].get_value(name)
            index += 1

        return value

    def using_nvme(self):
        """Is the configuration file setup to use NVMe devices.

        Returns:
            bool: True if NVMe devices are configured for at least one server in
                the config file; False otherwise

        """
        for server_params in self.server_params:
            if server_params.using_nvme():
                return True
        return False

    def using_scm(self):
        """Is the configuration file setup to use SCM devices.

        Returns:
            bool: True if SCM devices are configured for at least one server in
                the config file; False otherwise

        """
        for server_params in self.server_params:
            if server_params.using_scm():
                return True
        return False

    class PerServerYamlParameters(YamlParameters):
        """Defines the configuration yaml parameters for a single server."""

        def __init__(self):
            """Create a SingleServerConfig object."""
            super(ServerYamlParameters.PerServerYamlParameters, self).__init__(
                "/run/server_config/servers/*")

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
            self.fabric_iface = BasicParameter(
                None, os.getenv("OFI_INTERFACE", "eth0"))
            self.fabric_iface_port = BasicParameter(None, 31416)
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
                ServerYamlParameters.PerServerYamlParameters,
                self).get_params(test)

            # Override the log file file name with the test log file name
            if hasattr(test, "server_log") and test.server_log is not None:
                self.log_file.value = test.server_log

        def using_nvme(self):
            """Is the configuration file setup to use NVMe devices.

            Returns:
                bool: True if NVMe devices are configured; False otherwise

            """
            return self.bdev_class.value == "nvme"

        def using_scm(self):
            """Is the configuration file setup to use SCM devices.

            Returns:
                bool: True if SCM devices are configured; False otherwise

            """
            return self.scm_class.value == "dcpm"


class ServerCommand(DaosYamlCommand):
    """Defines an object representing a daso_server command."""

    def __init__(self, yaml_config, path="", timeout=30):
        """Create a daos_server command object.

        Args:
            server_config (ServerYamlParameters): server yaml configuration
            path (str): path to location of daos_server binary
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 60 seconds.
        """
        super(ServerCommand, self).__init__(
            "/run/daos_server/*", "daos_server", yaml_config, path, timeout)
        self.pattern = "DAOS I/O server.*started"

        # Additional daos_server command line parameters:
        #     --allow-proxy  Allow proxy configuration via environment
        self.allow_proxy = FormattedParameter("--allow-proxy", False)

        # If set the action_override value is used in place of the action.value
        self.action_override = None

    def get_params(self, test):
        """Get values for the daos agent command and its yaml config file.

        Args:
            test (Test): avocado Test object
        """
        super(ServerCommand, self).get_params(test)
        self.update_pattern()

    def get_action_command(self):
        """Set the action command object based on the yaml provided value."""
        if self.action_override is not None:
            # Use the override action if set and ignore the action parameter
            action = self.action_override
        else:
            # Use the action parameter if an override is not set
            action = self.action.value

        if action == "start":
            self.action_command = self.ServerStartSubCommand()
        else:
            self.action_command = None

    def update_pattern(self):
        """Update the paatern used to determine if the daos_server started."""
        if self.using_nvme() or self.using_scm():
            self.pattern = "SCM format required"
        else:
            self.pattern = "DAOS I/O server.*started"

    def using_nvme(self):
        """Is the daos command setup to use NVMe devices.

        Returns:
            bool: True if NVMe devices are configured; False otherwise

        """
        return self.yaml.using_nvme()

    def using_scm(self):
        """Is the daos command setup to use SCM devices.

        Returns:
            bool: True if SCM devices are configured; False otherwise

        """
        return self.yaml.using_scm()

    class ServerStartSubCommand(CommandWithParameters):
        """Defines an object representing a daos_server start sub command."""

        def __init__(self):
            """Create a start subcommand object."""
            super(ServerCommand.ServerStartSubCommand, self).__init__(
                "/run/daos_server/start/*", "start")
            self.port = FormattedParameter("-p {}")
            self.storage = FormattedParameter("-s {}")
            self.modules = FormattedParameter("-m {}")
            self.targets = FormattedParameter("-t {}")
            self.xshelpernr = FormattedParameter("-x {}")
            self.firstcore = FormattedParameter("-f {}")
            self.group = FormattedParameter("-g {}")
            self.attach = FormattedParameter("-a {}")
            self.sock_dir = FormattedParameter("-d {}")
            self.insecure = FormattedParameter("-i", False)
            self.recreate = FormattedParameter("--recreate-superblocks", True)


class ServerManager(SubprocessManager):
    """Manages the daos_server execution on one or more hosts using orterun."""

    def __init__(self, ompi_path, server_command):
        """Initialize a ServerManager object.

        Args:
            ompi_path (str): path to location of orterun binary.
            server_command (ServerCommand): server command object
        """
        super(ServerManager, self).__init__(
            "/run/server_config", server_command, ompi_path)
        self.job.action_override = "start"
