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

from command_utils import BasicParameter, FormattedParameter, ExecutableCommand
from command_utils import ObjectWithParameters, CommandFailure
from command_utils import DaosCommand, Orterun
from general_utils import pcmd, get_file_path
from dmg_utils import storage_format, storage_reset
from write_host_file import write_host_file

SESSIONS = {}

DEFAULT_FILE = "src/tests/ftest/data/daos_server_baseline.yaml"
AVOCADO_FILE = "src/tests/ftest/data/daos_avocado_test.yaml"


class ServerFailed(Exception):
    """Server didn't start/stop properly."""


class DaosServer(DaosCommand):
    """Defines an object representing a server command."""

    def __init__(self, path=""):
        """Create a server command object

        Args:
            path (str): path to location of daos_server binary.
        """
        super(DaosServer, self).__init__(
            "/run/daos_server/*", "daos_server", path)

        self.yaml_params = self.DaosServerConfig()
        self.timeout = 30
        self.server_cnt = 1
        self.mode = "normal"

        self.debug = FormattedParameter("-b", None)
        self.targets = FormattedParameter("-t {}")
        self.config = FormattedParameter("-o {}")
        self.port = FormattedParameter("-p {}")
        self.storage = FormattedParameter("-s {}")
        self.modules = FormattedParameter("-m {}")
        self.xshelpernr = FormattedParameter("-x {}")
        self.firstcore = FormattedParameter("-f {}")
        self.group = FormattedParameter("-g {}")
        self.attach = FormattedParameter("-a {}")
        self.sock_dir = FormattedParameter("-d {}")
        self.insecure = FormattedParameter("-i", None)

    def get_params(self, test):
        """Get params for Server object and server configuration."""
        super(DaosServer, self).get_params(test)
        self.yaml_params.get_params(test)

    def set_config(self, basepath):
        """Set the config value of the parameters in server command."""
        self.config.value = self.yaml_params.create_yaml(basepath)
        self.mode = "normal"
        if self.yaml_params.is_nvme() or self.yaml_params.is_scm():
            self.mode = "format"

    def check_subprocess_status(self, subprocess):
        """Wait for message from command output."""
        patterns = {
            "format": "SCM format required",
            "normal": "I/O server instance.*storage ready",
        }
        start_time = time.time()
        start_msgs = 0
        timed_out = False
        while start_msgs != self.server_cnt and not timed_out:
            output = subprocess.get_stdout()
            start_msgs = len(re.findall(patterns[self.mode], output))
            timed_out = time.time() - start_time > self.timeout

        if start_msgs != self.server_cnt:
            err_msg = "{} detected. Only {}/{} messages received".format(
                "Time out" if timed_out else "Error",
                start_msgs, self.server_cnt)
            print("{}:\n{}".format(err_msg, subprocess.get_stdout()))
            return False

        print("  Started server in <{}> mode in {} seconds".format(
            self.mode, time.time() - start_time))
        return True

    class DaosServerConfig(ObjectWithParameters):
        """Object to manage the configuration of the server command."""
        # pylint: disable=pylint-no-self-use

        def __init__(self):
            """ Create a DaosServerConfig object."""
            super(DaosServer.DaosServerConfig, self).__init__(
                "/run/server_config/*")
            self.data = None
            self.name = BasicParameter(None, "daos_server")
            self.port = BasicParameter(None, "10001")
            self.nvme = None
            self.scm = None

        @property
        def name(self):
            """Get the name from the server config."""
            if self.data and "name" in self.data:
                return self.data["name"]
            return None

        @name.setter
        def name(self, value):
            """Set the server config name attribute."""
            if self.data and "name" in self.data:
                self.data["name"] = value

        @property
        def port(self):
            """Get the port from the server config."""
            if self.data and "port" in self.data:
                return self.data["port"]
            return None

        @port.setter
        def port(self, value):
            """Set the port config attribute."""
            if self.data and "port" in self.data:
                self.data["port"] = value

        def get_params(self, test):
            """Get values for all of the command params from the yaml file.

            If no key matches are found in the yaml file the BasicParameter
            object will be set to its default value.

            Args:
                test (Test): avocado Test object
            """
            super(DaosServer.DaosServerConfig, self). get_params(test)
            # Read the baseline conf file data/daos_server_baseline.yml
            try:
                with open('{}/{}'.format(test.prefix, DEFAULT_FILE), 'r') as rf:
                    self.data = yaml.safe_load(rf)
            except Exception as err:
                print("<SERVER> Exception occurred: {0}".format(str(err)))
                traceback.print_exception(
                    err.__class__, err, sys.exc_info()[2])
                raise ServerFailed(
                    "Failed to Read {}/{}".format(test.prefix, DEFAULT_FILE))

            # Read the values from avocado_testcase.yaml file if test ran
            # with Avocado.
            new_value_set = {}
            if "AVOCADO_TEST_DATADIR" in os.environ:
                avocado_yaml_file = str(
                    os.environ["AVOCADO_TEST_DATADIR"]).split(".")[0] + ".yaml"

                # Read avocado test yaml file.
                try:
                    with open(avocado_yaml_file, 'r') as rfile:
                        filedata = rfile.read()
                    # Remove !mux for yaml load
                    new_value_set = yaml.safe_load(
                        filedata.replace('!mux', ''))
                except Exception as err:
                    print("<SERVER> Exception occurred: {0}".format(str(err)))
                    traceback.print_exception(
                        err.__class__, err, sys.exc_info()[2])
                    raise ServerFailed(
                        "Failed to Read {}".format(
                            '{}.tmp'.format(avocado_yaml_file)))

            # Update values from avocado_testcase.yaml in DAOS yaml variables.
            if new_value_set:
                if 'server' in new_value_set['server_config']:
                    for key in new_value_set['server_config']['server']:
                        self.data['servers'][0][key] = \
                            new_value_set['server_config']['server'][key]
                for key in new_value_set['server_config']:
                    if 'server' not in key:
                        self.data[key] = \
                            new_value_set['server_config'][key]

            # Check if nvme and scm are enabled
            srv_cfg = self.data['servers'][0]
            if 'bdev_class' in srv_cfg and srv_cfg['bdev_class'] == "nvme":
                self.nvme = True
            if 'scm_class' in srv_cfg and srv_cfg['scm_class'] == "dcpm":
                self.scm = True

            # if specific log file name specified use that
            if test.server_log:
                self.data['servers'][0]['log_file'] = test.server_log

        def is_nvme(self):
            """Return if NVMe is provided in the configuration."""
            return self.nvme is not None

        def is_scm(self):
            """Return if SCM (ram) is provided in the configuration."""
            return self.scm is not None

        def create_yaml(self, basepath):
            """Create the DAOS server config YAML file based on Avocado test
                Yaml file.

            Args:
                basepath (str): DAOS install basepath
                log_filename (str): Set a specific logfile name

            Raises:
                ServerFailed: if there is an reading/writing yaml files

            Returns:
                (str): Absolute path of create server yaml file

            """
            # Write self.data dictionary in to AVOCADO_FILE
            # This will be used to start with daos_server -o option.
            try:
                with open('{}/{}'.format(basepath, AVOCADO_FILE), 'w') as wfile:
                    yaml.dump(
                        self.data, wfile, default_flow_style=False)
            except Exception as err:
                print("<SERVER> Exception occurred: {0}".format(str(err)))
                traceback.print_exception(
                    err.__class__, err, sys.exc_info()[2])
                raise ServerFailed("Failed to Write {}/{}".format(
                    basepath, AVOCADO_FILE))

            return os.path.join(basepath, AVOCADO_FILE)


class ServerManager(ExecutableCommand):
    """Defines object to manage server functions and launch server command."""
    # pylint: disable=pylint-no-self-use, pylint-protected-access

    def __init__(self, basepath, runnerpath, attach="/tmp", timeout=30,
                 enable_path=False):
        """Create a ServerManager object.

        Args:
            basepath (str): Path to daos install
            runnerpath (str): Path to Orterun binary.
            attach (str, optional): Defaults to "/tmp".
            timeout (int, optional): Time for the server to start.
                Defaults to 30.
            enable_path (bool): If true, add PATH to orterun export.
        """
        super(ServerManager, self).__init__("/run/server_manager/*", "", "")

        self.basepath = basepath
        self._hosts = None

        # Setup orterun command defaults
        self.runner = Orterun(
            DaosServer(os.path.join(self.basepath, "bin")), runnerpath, True)
        self.runner.enable_recovery.value = True
        if enable_path:
            self.runner.export.value = ["PATH"]

        # Setup server command defaults
        self.runner.job.attach.value = attach
        self.runner.job.debug.value = True
        self.runner.job.insecure.value = True
        self.runner.job.request.value = "start"
        self.runner.job.sudo = True
        self.runner.job.timeout = timeout

        # Parameters that user can specify in the test yaml to modify behavior.
        self.debug = BasicParameter(None)           # ServerCommand param
        self.insecure = BasicParameter(None, True)  # ServerCommand param
        self.sudo = BasicParameter(None, True)      # ServerCommand param
        self.report_uri = BasicParameter(None)      # Orterun param
        self.export = BasicParameter(None)          # Orterun param
        self.processes = BasicParameter(None)       # Orterun param
        self.env = BasicParameter(None)             # Orterun param

    @property
    def hosts(self):
        """Hosts attribute getter."""
        return self._hosts

    @hosts.setter
    def hosts(self, value):
        """Hosts attribute setter

        Args:
            value (tuple): (list of hosts, workdir, slots)
        """
        self._hosts, workdir, slots = value
        self.runner.processes.value = len(self._hosts)
        self.runner.hostfile.value = write_host_file(
            self._hosts, workdir, slots)
        self.runner.job.server_cnt = len(self._hosts)

    def get_params(self, test):
        """Get values from the yaml file and assign them respectively
            to the server command and the orterun command.

        Args:
            test (Test): avocado Test object
        """
        server_params = ["attach", "debug", "insecure", "sudo"]
        runner_params = ["enable_recovery", "export"]
        super(ServerManager, self).get_params(test)
        self.runner.job.yaml_params.get_params(test)
        self.runner.get_params(test)
        for name in self.get_param_names():
            if name in server_params:
                if name == "sudo":
                    setattr(self.runner.job, name, getattr(self, name).value)
                else:
                    getattr(
                        self.runner.job, name).value = getattr(self, name).value
            if name in runner_params:
                getattr(self.runner, name).value = getattr(self, name).value

    def run(self):
        """Execute the runner subprocess."""
        return self.runner.run()

    def start(self):
        """Start the server through the runner."""

        self.runner.job.set_config(self.basepath)
        self.server_clean()

        # Prepare nvme storage in servers
        if self.runner.job.mode == "format":
            storage_prepare(self._hosts)

        try:
            self.run()
        except CommandFailure as details:
            print("<SERVER> Exception occurred: {}".format(details))
            # Kill the subprocess, anything that might have started
            raise ServerFailed(
                "Failed to start server in {} mode.".format(
                    self.runner.job.mode))

        if self.runner.job.mode == "format":
            servers_with_ports = [
                "{}:{}".format(host, self.runner.job.yaml_params.port)
                for host in self._hosts]

            storage_format(
                os.path.join(self.basepath, "bin"),
                ",".join(servers_with_ports))
            self.runner.job.mode = "normal"
            try:
                self.runner.job.check_subprocess_status(self.runner.process)
            except CommandFailure as details:
                print("Failed to start after format: {}".format(details))

        return True

    def stop(self):
        """Stop the server through the runner."""
        if self.runner.job.yaml_params.is_nvme() or \
            self.runner.job.yaml_params.is_scm():
            servers_with_ports = [
                "{}:{}".format(host, self.runner.job.yaml_params.port)
                for host in self._hosts]
            storage_reset(
                os.path.join(self.basepath, "bin"),
                ",".join(servers_with_ports))

        print("Stopping servers")
        error_list = []
        try:
            self.runner.stop()
        except CommandFailure as error:
            error_list.append("Error stopping servers: {}".format(error))

        return error_list

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
        # Intentionally ignoring the exit status of the command
        pcmd(self._hosts, "; ".join(kill_cmds), False, None, None)

    def clean_files(self):
        """Clean the tmpfs on the servers."""
        clean_cmds = [
            "find /mnt/daos -mindepth 1 -maxdepth 1 -print0 | xargs -0r rm -rf"
        ]

        if self.runner.job.yaml_params.is_nvme() or \
            self.runner.job.yaml_params.is_scm():
            clean_cmds.append("sudo umount /mnt/daos; sudo rm -rf /mnt/daos")

        # Intentionally ignoring the exit status of the command
        pcmd(self._hosts, "; ".join(clean_cmds), False, None, None)


def storage_prepare(hosts):
    """
    Prepare the storage on servers using the DAOS server's yaml settings file.
    Args:
        hosts (str): a string of comma-separated host names
    Raises:
        ServerFailed: if server failed to prepare storage
    """
    daos_srv_bin = get_file_path("bin/daos_server")
    cmd = ("sudo {} storage prepare -n --target-user=\"{}\" --hugepages=4096 -f"
           .format(daos_srv_bin[0], getpass.getuser()))
    result = pcmd(hosts, cmd, timeout=120)
    if len(result) > 1 or 0 not in result:
        raise ServerFailed("Error preparing NVMe storage")
