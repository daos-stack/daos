#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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
import os

from command_utils_base import \
    BasicParameter, LogParameter, YamlParameters, TransportCredentials


class DaosServerTransportCredentials(TransportCredentials):
    # pylint: disable=too-few-public-methods
    """Transport credentials listing certificates for secure communication."""

    def __init__(self, log_dir="/tmp"):
        """Initialize a TransportConfig object."""
        super(DaosServerTransportCredentials, self).__init__(
            "/run/server_config/transport_config/*",
            "transport_config", log_dir)

        # Additional daos_server transport credential parameters:
        #   - client_cert_dir: <str>, e.g. "".daos/clients"
        #       Location of client certificates [daos_server only]
        #
        self.client_cert_dir = LogParameter(log_dir, None, "clients")
        self.cert = LogParameter(log_dir, None, "server.crt")
        self.key = LogParameter(log_dir, None, "server.key")

    def get_certificate_data(self, name_list):
        """Get certificate data.

        Args:
            name_list (list): list of certificate attribute names.

        Returns:
            data (dict): a dictionary of parameter directory name keys and
                value.

        """
        # Ensure the client cert directory includes the required certificate
        name_list.remove("client_cert_dir")
        data = super(
            DaosServerTransportCredentials, self).get_certificate_data(
                name_list)
        if not self.allow_insecure.value and self.client_cert_dir.value:
            if self.client_cert_dir.value not in data:
                data[self.client_cert_dir.value] = ["agent.crt"]
            else:
                data[self.client_cert_dir.value].append("agent.crt")
        return data


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

        # Used to drop privileges before starting data plane
        # (if started as root to perform hardware provisioning)
        self.user_name = BasicParameter(None)
        self.group_name = BasicParameter(None)

        # Defines the number of single server config parameters to define in
        # the yaml file
        self.servers_per_host = BasicParameter(None)

        # Single server config parameters. Default to one set of I/O server
        # parameters - for the config_file_gen.py tool. Calling get_params()
        # will update the list to match the number of I/O servers requested by
        # the self.servers_per_host.value.
        self.server_params = [self.PerServerYamlParameters()]

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        If no key matches are found in the yaml file the BasicParameter object
        will be set to its default value.

        Args:
            test (Test): avocado Test object
        """
        super(DaosServerYamlParameters, self).get_params(test)

        # Create the requested number of single server parameters
        if isinstance(self.servers_per_host.value, int):
            self.server_params = [
                self.PerServerYamlParameters(index)
                for index in range(self.servers_per_host.value)]
        else:
            self.server_params = [self.PerServerYamlParameters()]

        for server_params in self.server_params:
            server_params.get_params(test)

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        # Get the common config yaml parameters
        yaml_data = super(DaosServerYamlParameters, self).get_yaml_data()

        # Remove the "servers_per_host" BasicParameter as it is not an actual
        # daos_server configuration file parameter
        yaml_data.pop("servers_per_host", None)

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
            default_share_addr = int(os.environ.get("CRT_CTX_SHARE_ADDR", 0))
            default_provider = os.environ.get("CRT_PHY_ADDR_STR", "ofi+sockets")

            # All log files should be placed in the same directory on each host
            # to enable easy log file archiving by launch.py
            log_dir = os.environ.get("DAOS_TEST_LOG_DIR", "/tmp")

            # Parameters
            #   targets:                count of VOS targets
            #   first_core:             starting index for targets
            #   nr_xs_helpers:          offload helpers per server
            #   fabric_iface:           map to OFI_INTERFACE=eth0
            #   fabric_iface_port:      map to OFI_PORT=31416
            #   log_mask:               map to D_LOG_MASK env
            #   log_file:               map to D_LOG_FILE env
            #   env_vars:               influences DAOS IO Server behavior
            #       Add to enable scalable endpoint:
            #           - CRT_CTX_SHARE_ADDR=1
            #           - CRT_CTX_NUM=8
            #       nvme options:
            #           - IO_STAT_PERIOD=10
            self.targets = BasicParameter(None, 8)
            self.first_core = BasicParameter(None, 0)
            self.nr_xs_helpers = BasicParameter(None, 16)
            self.fabric_iface = BasicParameter(None, default_interface)
            self.fabric_iface_port = BasicParameter(None, default_port)
            self.pinned_numa_node = BasicParameter(None)
            self.log_mask = BasicParameter(None, "INFO")
            self.log_file = LogParameter(log_dir, None, "daos_server.log")

            # Set extra environment variables for sockets provider
            default_env_vars = [
                "ABT_ENV_MAX_NUM_XSTREAMS=100",
                "ABT_MAX_NUM_XSTREAMS=100",
                "DAOS_MD_CAP=1024",
                "DD_MASK=mgmt,io,md,epc,rebuild"
            ]
            if default_provider == "ofi+sockets":
                default_env_vars.extend([
                    "FI_SOCKETS_MAX_CONN_RETRY=5",
                    "FI_SOCKETS_CONN_TIMEOUT=2000",
                    "CRT_SWIM_RPC_TIMEOUT=10"
                ])
            self.env_vars = BasicParameter(None, default_env_vars)

            # global CRT_CTX_SHARE_ADDR shared with client
            self.crt_ctx_share_addr = BasicParameter(None, default_share_addr)

            # global CRT_TIMEOUT shared with client
            self.crt_timeout = BasicParameter(None, 30)

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
            self.scm_size = BasicParameter(None, 16)
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
