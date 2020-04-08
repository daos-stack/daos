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
# pylint: disable=too-many-lines
from __future__ import print_function

import traceback
import sys
import os
import re
import time
import yaml
import getpass

# Remove below imports when depricating run_server and stop_server functions.
import subprocess
import json
import resource
import signal
import fcntl
import errno
from avocado.utils import genio
from distutils.spawn import find_executable
# Remove above imports when depricating run_server and stop_server functions.

from command_utils import BasicParameter, FormattedParameter, ExecutableCommand
from command_utils import ObjectWithParameters, CommandFailure
from command_utils import DaosCommand, Orterun, CommandWithParameters
from general_utils import pcmd
from dmg_utils import storage_format
from write_host_file import write_host_file
from env_modules import load_mpi

SESSIONS = {}

AVOCADO_FILE = "daos_avocado_test.yaml"


class ServerFailed(Exception):
    """Server didn't start/stop properly."""


class DaosServer(DaosCommand):
    """Defines an object representing a server command."""

    def __init__(self, path=""):
        """Create a server command object.

        Args:
            path (str): path to location of daos_server binary.
        """
        super(DaosServer, self).__init__(
            "/run/daos_server/*", "daos_server", path)

        self.yaml_params = DaosServerConfig()
        self.timeout = 120
        self.server_list = []
        self.mode = "normal"

        self.debug = FormattedParameter("-b", True)
        self.json = FormattedParameter("-j", False)
        self.config = FormattedParameter("-o {}")

    def get_params(self, test):
        """Get params for Server object and server configuration."""
        super(DaosServer, self).get_params(test)
        self.yaml_params.get_params(test)

    def get_action_command(self):
        """Set the action command object based on the yaml provided value."""
        if self.action.value == "start":
            self.action_command = self.ServerStartSubCommand()
        else:
            self.action_command = None

    def set_config(self, yamlfile):
        """Set the config value of the parameters in server command."""
        access_points = ":".join((self.server_list[0],
                                  str(self.yaml_params.port)))
        self.yaml_params.access_points.value = access_points.split()
        self.config.value = self.yaml_params.create_yaml(yamlfile)

    def check_subprocess_status(self, sub_process):
        """Wait for message from command output.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command
        """
        server_count = len(self.server_list)
        patterns = {
            "format": "(SCM format required)(?!;)",
            "normal": "DAOS I/O server.*started",
        }
        expected = {
            "format": server_count,
            "normal": server_count * len(self.yaml_params.server_params),
        }
        detected = 0
        complete = False
        timed_out = False
        start_time = time.time()

        # Search for patterns in the 'daos_server start' output until:
        #   - the expected number of pattern matches are detected (success)
        #   - the time out is reached (failure)
        #   - the subprocess is no longer running (failure)
        while not complete and not timed_out and sub_process.poll() is None:
            output = sub_process.get_stdout()
            detected = len(re.findall(patterns[self.mode], output))
            complete = detected == expected[self.mode]
            timed_out = time.time() - start_time > self.timeout

        # Summarize results
        msg = "{}/{} {} messages detected in {}/{} seconds".format(
            detected, expected[self.mode], self.mode, time.time() - start_time,
            self.timeout)
        if not complete:
            self.log.info(
                "%s detected - %s:\n%s",
                "Time out" if timed_out else "Error",
                msg,
                sub_process.get_stdout() if not self.verbose else "<See above>")
        else:
            self.log.info("Server startup detected - %s", msg)

        return complete

    def get_config_value(self, name):
        """Get the value associated with a daos_server configuration value name.

        Args:
            name (str): configuration value name

        Raises:
            ServerFailure: if the configuration value name does not exist

        Returns:
            str: value of the provided configuration name

        """
        return self.yaml_params.get_value(name)

    class ServerStartSubCommand(CommandWithParameters):
        """Defines an object representing a daos_server start sub command."""

        def __init__(self):
            """Create a start subcommand object."""
            super(DaosServer.ServerStartSubCommand, self).__init__(
                "/run/daos_server/start/*", "start")
            self.port = FormattedParameter("-p {}")
            self.storage = FormattedParameter("-s {}")
            self.modules = FormattedParameter("-m {}")
            self.targets = FormattedParameter("-t {}")
            self.xshelpernr = FormattedParameter("-x {}")
            self.firstcore = FormattedParameter("-f {}")
            self.group = FormattedParameter("-g {}")
            self.sock_dir = FormattedParameter("-d {}")
            self.insecure = FormattedParameter("-i", True)
            self.recreate = FormattedParameter("--recreate-superblocks", False)


class DaosServerConfig(ObjectWithParameters):
    """Defines the daos_server configuration yaml parameters."""

    class SingleServerConfig(ObjectWithParameters):
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
            super(DaosServerConfig.SingleServerConfig, self).__init__(namespace)

            # Use environment variables to get default parameters
            default_interface = os.environ.get("OFI_INTERFACE", "eth0")
            default_port = os.environ.get("OFI_PORT", 31416)

            # Parameters
            #   targets:                count of VOS targets
            #   first_core:             starting index for targets
            #   nr_xs_helpers:          offload helpers per server
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
            self.nr_xs_helpers = BasicParameter(None, 16)
            self.fabric_iface = BasicParameter(None, default_interface)
            self.fabric_iface_port = BasicParameter(None, default_port)
            self.pinned_numa_node = BasicParameter(None)
            self.log_mask = BasicParameter(None, "DEBUG,RPC=ERR")
            self.log_file = BasicParameter(None, "daos_server.log")
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
            self.scm_size = BasicParameter(None, 16)
            self.scm_list = BasicParameter(None)
            self.bdev_class = BasicParameter(None)
            self.bdev_list = BasicParameter(None)
            self.bdev_size = BasicParameter(None)
            self.bdev_number = BasicParameter(None)

    def __init__(self):
        """Create a DaosServerConfig object."""
        super(DaosServerConfig, self).__init__("/run/server_config/*")

        # Parameters
        self.name = BasicParameter(None, "daos_server")
        self.access_points = BasicParameter(None)       # e.g. "<host>:<port>"
        self.port = BasicParameter(None, 10001)
        self.provider = BasicParameter(None, "ofi+sockets")
        self.socket_dir = BasicParameter(None)          # /tmp/daos_sockets
        self.nr_hugepages = BasicParameter(None, 4096)
        self.control_log_mask = BasicParameter(None, "DEBUG")
        self.control_log_file = BasicParameter(None, "daos_control.log")
        self.helper_log_file = BasicParameter(None, "daos_admin.log")

        # Used to drop privileges before starting data plane
        # (if started as root to perform hardware provisioning)
        self.user_name = BasicParameter(None)           # e.g. 'daosuser'
        self.group_name = BasicParameter(None)          # e.g. 'daosgroup'

        # Defines the number of single server config parameters to define in
        # the yaml file
        self.servers_per_host = BasicParameter(None)

        # Single server config parameters
        self.server_params = []

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        If no key matches are found in the yaml file the BasicParameter object
        will be set to its default value.

        Args:
            test (Test): avocado Test object
        """
        super(DaosServerConfig, self).get_params(test)

        # Create the requested number of single server parameters
        if isinstance(self.servers_per_host.value, int):
            self.server_params = [
                self.SingleServerConfig(index)
                for index in range(self.servers_per_host.value)]
        else:
            self.server_params = [self.SingleServerConfig()]

        for server_params in self.server_params:
            server_params.get_params(test)

    def update_log_files(self, control_log, helper_log, server_log):
        """Update each log file name for the daos server.

        If there are multiple server configurations defined the server_log value
        will be made unique for each server's log_file parameter.

        Any log file name set to None will result in no update to the respective
        log file parameter value.

        Args:
            control_log (str): control log file name
            helper_log (str): control log file name
            server_log (str): control log file name
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
                    log_name.insert(1, "_{}".format(index))
                server_params.log_file.update(
                    "".join(log_name),
                    "server_config.server[{}].log_file".format(index))

    def is_nvme(self):
        """Return if NVMe is provided in the configuration."""
        if self.server_params[-1].bdev_class.value == "nvme":
            return True
        return False

    def is_scm(self):
        """Return if SCM is provided in the configuration."""
        if self.server_params[-1].scm_class.value == "dcpm":
            return True
        return False

    def create_yaml(self, filename):
        """Create a yaml file from the parameter values.

        Args:
            filename (str): the yaml file to create
        """
        log_dir = os.environ.get("DAOS_TEST_LOG_DIR", "/tmp")

        # Convert the parameters into a dictionary to write a yaml file
        yaml_data = {"servers": []}
        for name in self.get_param_names():
            if name != "servers_per_host":
                value = getattr(self, name).value
                if value is not None and value is not False:
                    if name.endswith("log_file"):
                        yaml_data[name] = os.path.join(
                            log_dir, value)
                    else:
                        yaml_data[name] = value
        for server_params in self.server_params:
            yaml_data["servers"].append({})
            for name in server_params.get_param_names():
                value = getattr(server_params, name).value
                if value is not None and value is not False:
                    if name.endswith("log_file"):
                        yaml_data["servers"][-1][name] = os.path.join(
                            log_dir, value)
                    else:
                        yaml_data["servers"][-1][name] = value

        # Don't set scm_size when scm_class is "dcpm"
        for index in range(len(self.server_params)):
            srv_cfg = yaml_data["servers"][index]
            scm_class = srv_cfg.get("scm_class", "ram")
            if scm_class == "dcpm" and "scm_size" in srv_cfg:
                del srv_cfg["scm_size"]

        # Write default_value_set dictionary in to AVOCADO_FILE
        # This will be used to start with daos_server -o option.
        try:
            with open(filename, 'w') as write_file:
                yaml.dump(yaml_data, write_file, default_flow_style=False)
        except Exception as error:
            print("<SERVER> Exception occurred: {0}".format(error))
            raise ServerFailed(
                "Error writing daos_server command yaml file {}: {}".format(
                    filename, error))
        return filename

    def get_value(self, name):
        """Get the value associated with the configuration name.

        Configuration names will first match any general configuration settings,
        followed by any single server configuration entries.

        Args:
            name (str): configuration name

        Raises:
            ServerFailed: a configuration setting matching the specified name
                was not found.

        Returns:
            [type]: [description]

        """
        found = False
        for obj in [self] + self.server_params:
            setting = getattr(obj, name, "setting-not-found")
            if isinstance(setting, BasicParameter):
                value = setting.value
                found = True
                break
            elif setting != "setting-not-found":
                value = setting
                found = True
                break
        if not found:
            raise ServerFailed(
                "No daos_server configuration value for {}".format(name))
        return value


class ServerManager(ExecutableCommand):
    """Defines object to manage server functions and launch server command."""

    # Mapping of environment variable names to daos_server config param names
    ENVIRONMENT_VARIABLE_MAPPING = {
        "CRT_PHY_ADDR_STR": "provider",
        "OFI_INTERFACE": "fabric_iface",
        "OFI_PORT": "fabric_iface_port",
    }

    def __init__(self, daosbinpath, runnerpath, timeout=300):
        """Create a ServerManager object.

        Args:
            daosbinpath (str): Path to daos bin
            runnerpath (str): Path to Orterun binary.
            timeout (int, optional): Time for the server to start.
                Defaults to 300.
        """
        super(ServerManager, self).__init__("/run/server_manager/*", "", "")

        self.daosbinpath = daosbinpath
        self._hosts = None

        # Setup orterun command defaults
        self.runner = Orterun(
            DaosServer(self.daosbinpath), runnerpath, True)

        # Setup server command defaults
        self.runner.job.action.value = "start"
        self.runner.job.get_action_command()

        # Parameters that user can specify in the test yaml to modify behavior.
        self.debug = BasicParameter(None, True)       # ServerCommand param
        self.insecure = BasicParameter(None, True)    # ServerCommand param
        self.recreate = BasicParameter(None, False)   # ServerCommand param
        self.sudo = BasicParameter(None, False)       # ServerCommand param
        self.srv_timeout = BasicParameter(None, timeout)   # ServerCommand param
        self.report_uri = BasicParameter(None)             # Orterun param
        self.enable_recovery = BasicParameter(None, True)  # Orterun param
        self.export = BasicParameter(None)                 # Orterun param

    @property
    def hosts(self):
        """Hosts attribute getter."""
        return self._hosts

    @hosts.setter
    def hosts(self, value):
        """Hosts attribute setter.

        Args:
            value (tuple): (list of hosts, workdir, slots)
        """
        self._hosts, workdir, slots = value
        self.runner.processes.value = len(self._hosts)
        self.runner.hostfile.value = write_host_file(
            self._hosts, workdir, slots)
        self.runner.job.server_list = self._hosts

    def get_params(self, test):
        """Get values from the yaml file.

        Assign the ServerManager parameters to their respective ServerCommand
        and Orterun class parameters.

        Args:
            test (Test): avocado Test object
        """
        server_params = ["debug", "sudo", "srv_timeout"]
        server_start_params = ["insecure", "recreate"]
        runner_params = ["enable_recovery", "export", "report_uri"]
        super(ServerManager, self).get_params(test)
        self.runner.job.yaml_params.get_params(test)
        self.runner.get_params(test)
        for name in self.get_param_names():
            if name in server_params:
                if name == "sudo":
                    setattr(self.runner.job, name, getattr(self, name).value)
                elif name == "srv_timeout":
                    setattr(
                        self.runner.job, "timeout", getattr(self, name).value)
                else:
                    getattr(
                        self.runner.job, name).value = getattr(self, name).value
            if name in server_start_params:
                getattr(self.runner.job.action_command, name).value = \
                    getattr(self, name).value
            if name in runner_params:
                getattr(self.runner, name).value = getattr(self, name).value

        # Run daos_server with test variant specific log file names if specified
        self.runner.job.yaml_params.update_log_files(
            getattr(test, "control_log"),
            getattr(test, "helper_log"),
            getattr(test, "server_log")
        )

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
            value = self.runner.job.get_config_value(setting)

        except IndexError:
            raise ServerFailed(
                "Unknown server config setting mapping for the {} environment "
                "variable!".format(name))

        return value

    def run(self):
        """Execute the runner subprocess."""
        self.log.info("Start CMD>>> %s", str(self.runner))

        # Temporary display debug mount information
        self.log.info("%s", "=" * 80)
        pcmd(self._hosts, "df -h -t tmpfs", True, None, None)
        self.log.info("%s", "=" * 80)

        return self.runner.run()

    def start(self, yamlfile):
        """Start the server through the runner."""
        storage_prep_flag = ""
        self.runner.job.set_config(yamlfile)
        self.server_clean()

        # Prepare SCM storage in servers
        if self.runner.job.yaml_params.is_scm():
            storage_prep_flag = "dcpm"
            self.log.info("Performing SCM storage prepare in <format> mode")
        else:
            storage_prep_flag = "ram"

        # Prepare nvme storage in servers
        if self.runner.job.yaml_params.is_nvme():
            if storage_prep_flag == "dcpm":
                storage_prep_flag = "dcpm_nvme"
            elif storage_prep_flag == "ram":
                storage_prep_flag = "ram_nvme"
            else:
                storage_prep_flag = "nvme"
            self.log.info("Performing NVMe storage prepare in <format> mode")
            # Make sure log file has been created for ownership change
            lfile = self.runner.job.yaml_params.server_params[-1].log_file.value
            if lfile is not None:
                self.log.info("Creating log file")
                cmd_touch_log = "touch {}".format(lfile)
                pcmd(self._hosts, cmd_touch_log, False)
        if storage_prep_flag != "ram":
            self.storage_prepare(getpass.getuser(), storage_prep_flag)
            self.runner.mca.update(
                {"plm_rsh_args": "-l root"}, "orterun.mca", True)

        # Start the server and wait for each host to require a SCM format
        self.runner.job.mode = "format"
        try:
            self.run()
        except CommandFailure as error:
            raise ServerFailed(
                "Failed to start servers before format: {}".format(error))

        # Format storage and wait for server to change ownership
        self.log.info("Formatting hosts: <%s>", self._hosts)
        servers_with_ports = [
            "{}:{}".format(host, self.runner.job.yaml_params.port)
            for host in self._hosts]
        storage_format(self.daosbinpath, ",".join(servers_with_ports))

        # Wait for all the doas_io_servers to start
        self.runner.job.mode = "normal"
        if not self.runner.job.check_subprocess_status(self.runner.process):
            raise ServerFailed("Failed to start servers after format")

        return True

    def stop(self):
        """Stop the server through the runner."""
        self.log.info("Stopping servers")
        if self.runner.job.yaml_params.is_nvme():
            self.kill()
            self.storage_reset()
            # Make sure the mount directory belongs to non-root user
            self.log.info("Changing ownership of mount to non-root user")
            cmd = "sudo chown -R {0}:{0} /mnt/daos*".format(getpass.getuser())
            pcmd(self._hosts, cmd, False)
        else:
            try:
                self.runner.stop()
            except CommandFailure as error:
                raise ServerFailed("Failed to stop servers:{}".format(error))

    def server_clean(self):
        """Prepare the hosts before starting daos server."""
        # Kill any doas servers running on the hosts
        self.kill()
        # Clean up any files that exist on the hosts
        self.clean_files()

    def kill(self):
        """Forcably kill any daos server processes running on hosts.

        Sometimes stop doesn't get everything.  Really whack everything
        with this.

        """
        kill_cmds = [
            "sudo pkill '(daos_server|daos_io_server)' --signal INT",
            "sleep 5",
            "pkill '(daos_server|daos_io_server)' --signal KILL",
        ]
        self.log.info("Killing any server processes")
        pcmd(self._hosts, "; ".join(kill_cmds), False, None, None)

    def clean_files(self):
        """Clean the tmpfs on the servers."""
        clean_cmds = []
        for server_params in self.runner.job.yaml_params.server_params:
            scm_mount = server_params.scm_mount.value
            self.log.info("Cleaning up the %s directory.", str(scm_mount))

            # Remove the superblocks
            cmd = "rm -fr {}/*".format(scm_mount)
            if cmd not in clean_cmds:
                clean_cmds.append(cmd)

            # Dismount the scm mount point
            cmd = "while sudo umount {}; do continue; done".format(scm_mount)
            if cmd not in clean_cmds:
                clean_cmds.append(cmd)

            if self.runner.job.yaml_params.is_scm():
                scm_list = server_params.scm_list.value
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
                    if cmd not in clean_cmds:
                        clean_cmds.append(cmd)

        pcmd(self._hosts, "; ".join(clean_cmds), True)

    def storage_prepare(self, user, device_type):
        """Prepare server's storage using the DAOS server's yaml settings file.

        Args:
            user (str): username for file permissions
            device_type (str): storage type - scm or nvme

        Raises:
            ServerFailed: if server failed to prepare storage

        """
        # Get the daos_server from the install path. Useful for testing
        # with daos built binaries.
        dev_param = ""
        device_args = ""
        daos_srv_bin = os.path.join(self.daosbinpath, "daos_server")
        if device_type == "dcpm":
            dev_param = "-s"
        elif device_type == "dcpm_nvme":
            device_args = " --hugepages=4096"
        elif device_type in ("ram_nvme", "nvme"):
            dev_param = "-n"
            device_args = " --hugepages=4096"
        else:
            raise ServerFailed("Invalid device type")
        cmd = "{} storage prepare {} -u \"{}\" {} -f".format(
            daos_srv_bin, dev_param, user, device_args)
        result = pcmd(self._hosts, cmd, timeout=120)
        if len(result) > 1 or 0 not in result:
            raise ServerFailed("Error preparing {} storage".format(device_type))

    def storage_reset(self):
        """Reset the servers' storage.

        NOTE: Don't enhance this method to reset SCM. SCM will not be in a
        useful state for running next tests.

        Raises:
            ServerFailed: if server failed to reset storage

        """
        daos_srv_bin = os.path.join(self.daosbinpath, "daos_server")
        cmd = "{} storage prepare -n --reset -f".format(daos_srv_bin)
        result = pcmd(self._hosts, cmd)
        if len(result) > 1 or 0 not in result:
            raise ServerFailed("Error resetting NVMe storage")


def run_server(test, hostfile, setname, uri_path=None, env_dict=None,
               clean=True):
    # pylint: disable=unused-argument
    """Launch DAOS servers in accordance with the supplied hostfile.

    Args:
        test (Test): avocado Test object
        hostfile (str): hostfile defining on which hosts to start servers
        setname (str): session name
        uri_path (str, optional): path to uri file. Defaults to None.
        env_dict (dict, optional): dictionary on env variable names and values.
            Defaults to None.
        clean (bool, optional): clean the mount point. Defaults to True.

    Raises:
        ServerFailed: if there is an error starting the servers

    """
    global SESSIONS    # pylint: disable=global-variable-not-assigned
    try:
        servers = (
            [line.split(' ')[0] for line in genio.read_all_lines(hostfile)])
        server_count = len(servers)

        # Pile of build time variables
        with open("../../.build_vars.json") as json_vars:
            build_vars = json.load(json_vars)

        # Create the DAOS server configuration yaml file to pass
        # with daos_server -o <FILE_NAME>
        print("Creating the server yaml file in {}".format(test.tmp))
        server_yaml = os.path.join(test.tmp, AVOCADO_FILE)
        server_config = DaosServerConfig()
        server_config.get_params(test)
        access_points = ":".join((servers[0], str(server_config.port)))
        server_config.access_points.value = access_points.split()
        server_config.update_log_files(
            getattr(test, "control_log"),
            getattr(test, "helper_log"),
            getattr(test, "server_log")
        )
        server_config.create_yaml(server_yaml)

        # first make sure there are no existing servers running
        print("Removing any existing server processes")
        kill_server(servers)

        # clean the tmpfs on the servers
        if clean:
            print("Cleaning the server tmpfs directories")
            result = pcmd(
                servers,
                "find /mnt/daos -mindepth 1 -maxdepth 1 -print0 | "
                "xargs -0r rm -rf",
                verbose=False)
            if len(result) > 1 or 0 not in result:
                raise ServerFailed(
                    "Error cleaning tmpfs on servers: {}".format(
                        ", ".join(
                            [str(result[key]) for key in result if key != 0])))
        load_mpi('openmpi')
        orterun_bin = find_executable('orterun')
        if orterun_bin is None:
            raise ServerFailed("Can't find orterun")

        server_cmd = [orterun_bin, "--np", str(server_count)]
        server_cmd.extend(["--mca", "btl_openib_warn_default_gid_prefix", "0"])
        server_cmd.extend(["--mca", "btl", "tcp,self"])
        server_cmd.extend(["--mca", "oob", "tcp"])
        server_cmd.extend(["--mca", "pml", "ob1"])
        server_cmd.extend(["--hostfile", hostfile])
        server_cmd.extend(["--enable-recovery", "--tag-output"])

        # Add any user supplied environment
        if env_dict is not None:
            for key, value in env_dict.items():
                os.environ[key] = value
                server_cmd.extend(["-x", "{}={}".format(key, value)])

        # the remote orte needs to know where to find daos, in the
        # case that it's not in the system prefix
        # but it should already be in our PATH, so just pass our
        # PATH along to the remote
        if build_vars["PREFIX"] != "/usr":
            server_cmd.extend(["-x", "PATH"])

        # Run server in insecure mode until Certificate tests are in place
        server_cmd.extend(
            [os.path.join(build_vars["PREFIX"], "bin", "daos_server"),
             "--debug",
             "--config", server_yaml,
             "start", "-i", "--recreate-superblocks"])

        print("Start CMD>>>>{0}".format(' '.join(server_cmd)))

        resource.setrlimit(
            resource.RLIMIT_CORE,
            (resource.RLIM_INFINITY, resource.RLIM_INFINITY))

        SESSIONS[setname] = subprocess.Popen(server_cmd,
                                             stdout=subprocess.PIPE,
                                             stderr=subprocess.PIPE)
        fdesc = SESSIONS[setname].stdout.fileno()
        fstat = fcntl.fcntl(fdesc, fcntl.F_GETFL)
        fcntl.fcntl(fdesc, fcntl.F_SETFL, fstat | os.O_NONBLOCK)
        timeout = 600
        start_time = time.time()
        matches = 0
        pattern = "DAOS I/O server.*started"
        expected_data = "Starting Servers\n"
        while True:
            output = ""
            try:
                output = SESSIONS[setname].stdout.read()
            except IOError as excpn:
                if excpn.errno != errno.EAGAIN:
                    raise ServerFailed("Server didn't start: {}".format(excpn))
                continue
            match = re.findall(pattern, output)
            expected_data += output
            matches += len(match)
            if not output or matches == server_count or \
               time.time() - start_time > timeout:
                print("<SERVER>: {}".format(expected_data))
                if matches != server_count:
                    raise ServerFailed("Server didn't start!")
                break
        print(
            "<SERVER> server started and took {} seconds to start".format(
                time.time() - start_time))

    except Exception as error:
        print("<SERVER> Exception occurred: {0}".format(str(error)))
        traceback.print_exception(error.__class__, error, sys.exc_info()[2])
        # We need to end the session now -- exit the shell
        try:
            SESSIONS[setname].send_signal(signal.SIGINT)
            time.sleep(5)
            # get the stderr
            error = SESSIONS[setname].stderr.read()
            if SESSIONS[setname].poll() is None:
                SESSIONS[setname].kill()
            retcode = SESSIONS[setname].wait()
            print(
                "<SERVER> server start return code: {}\nstderr:\n{}".format(
                    retcode, error))
        except KeyError:
            pass
        raise ServerFailed("Server didn't start!")


def stop_server(setname=None, hosts=None):
    """Stop the daos servers.

    Attempt to initiate an orderly shutdown of all orterun processes it has
    spawned by sending a ctrl-c to the process matching the setname (or all
    processes if no setname is provided).

    If a list of hosts is provided, verify that all daos server processes are
    dead.  Report an error if any processes are found and attempt to forcably
    kill the processes.

    Args:
        setname (str, optional): server group name used to match the session
            used to start the server. Defaults to None.
        hosts (list, optional): list of hosts running the server processes.
            Defaults to None.

    Raises:
        ServerFailed: if there was an error attempting to send a signal to stop
            the processes running the servers or after sending the signal if
            there are processes stiull running.

    """
    global SESSIONS    # pylint: disable=global-variable-not-assigned
    try:
        if setname is None:
            for _key, val in SESSIONS.items():
                val.send_signal(signal.SIGINT)
                time.sleep(5)
                if val.poll() is None:
                    val.kill()
                val.wait()
        else:
            SESSIONS[setname].send_signal(signal.SIGINT)
            time.sleep(5)
            if SESSIONS[setname].poll() is None:
                SESSIONS[setname].kill()
            SESSIONS[setname].wait()
        print("<SERVER> server stopped")

    except Exception as error:
        print("<SERVER> Exception occurred: {0}".format(str(error)))
        raise ServerFailed("Server didn't stop!")

    if not hosts:
        return

    # Make sure the servers actually stopped.  Give them time to stop first
    # pgrep exit status:
    #   0 - One or more processes matched the criteria.
    #   1 - No processes matched.
    #   2 - Syntax error in the command line.
    #   3 - Fatal error: out of memory etc.
    time.sleep(5)
    result = pcmd(
        hosts, "pgrep '(daos_server|daos_io_server)'", False, expect_rc=1)
    if len(result) > 1 or 1 not in result:
        bad_hosts = [
            node for key in result if key != 1 for node in list(result[key])]
        kill_server(bad_hosts)
        raise ServerFailed(
            "DAOS server processes detected after attempted stop on {}".format(
                ", ".join([str(result[key]) for key in result if key != 1])))

    # we can also have orphaned ssh processes that started an orted on a
    # remote node but never get cleaned up when that remote node spontaneiously
    # reboots
    subprocess.call(["pkill", "^ssh$"])


def kill_server(hosts):
    """Forcably kill any daos server processes running on the specified hosts.

    Sometimes stop doesn't get everything.  Really whack everything with this.

    Args:
        hosts (list): list of host names where servers are running
    """
    kill_cmds = [
        "pkill '(daos_server|daos_io_server)' --signal INT",
        "sleep 5",
        "pkill '(daos_server|daos_io_server)' --signal KILL",
    ]
    # Intentionally ignoring the exit status of the command
    pcmd(hosts, "; ".join(kill_cmds), False, None, None)
