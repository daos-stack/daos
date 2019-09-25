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
from command_utils import DaosCommand, Orterun, CommandFailure
from general_utils import pcmd, check_file_exists, get_file_path
from dmg_utils import storage_format, storage_reset
from write_host_file import write_host_file

SESSIONS = {}

DEFAULT_FILE = "src/tests/ftest/data/daos_server_baseline.yaml"
AVOCADO_FILE = "src/tests/ftest/data/daos_avocado_test.yaml"


class ServerFailed(Exception):
    """Server didn't start/stop properly."""


class ServerCommand(DaosCommand):
    """Defines an object representing a server command."""

    def __init__(self, path=""):
        """Create a server command object

        Args:
            path (str): path to location of daos_server binary.
        """
        super(ServerCommand, self).__init__(
            "/run/daos_server/*", "daos_server", path)

        self.yaml = self.ServerConfig()

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

    class ServerConfig(object):
        """Object to manage the configuration of the server command."""
        # pylint: disable=pylint-no-self-use

        def __init__(self):
            """ Create a ServerConfig object."""
            self.nvme = None
            self.scm = None

        def is_nvme(self):
            """Return if NVMe is provided in the configuration."""
            return self.nvme

        def is_scm(self):
            """Return if SCM (ram) is provided in the configuration."""
            return self.scm

        def _create_yaml(self, basepath, log_filename):
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
            # Read the baseline conf file data/daos_server_baseline.yml
            try:
                with open('{}/{}'.format(basepath, DEFAULT_FILE), 'r') as rfile:
                    default_value_set = yaml.safe_load(rfile)
            except Exception as err:
                print("<SERVER> Exception occurred: {0}".format(str(err)))
                traceback.print_exception(
                    err.__class__, err, sys.exc_info()[2])
                raise ServerFailed(
                    "Failed to Read {}/{}".format(basepath, DEFAULT_FILE))

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
                for key in new_value_set['server_config']:
                    if key in default_value_set['servers'][0]:
                        default_value_set['servers'][0][key] = \
                            new_value_set['server_config'][key]
                    elif key in default_value_set:
                        default_value_set[key] = \
                            new_value_set['server_config'][key]

            # Check if nvme is enabled
            if default_value_set['servers'][0]['bdev_class'] == "nvme":
                self.nvme = True

            if default_value_set['servers'][0]['scm_class'] == "ram":
                self.scm = True

            # if sepcific log file name specified use that
            if log_filename:
                default_value_set['servers'][0]['log_file'] = log_filename

            # Write default_value_set dictionary in to AVOCADO_FILE
            # This will be used to start with daos_server -o option.
            try:
                with open('{}/{}'.format(basepath, AVOCADO_FILE), 'w') as wfile:
                    yaml.dump(
                        default_value_set, wfile, default_flow_style=False)
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

    def __init__(self, basepath, runnerpath, attach="/tmp", timeout=120,
                 enable_path=False):
        """Create a ServerManager object.

        Args:
            basepath (str): Path to daos install
            runnerpath (str): Path to Orterun binary.
            attach (str, optional): Defaults to "/tmp".
            timeout (int, optional): Time for the server to start.
                Defaults to 120.
            enable_path (bool): If true, add PATH to orterun export.
        """
        super(ServerManager, self).__init__("/run/server_manager/*", "", "")

        self.mode = "normal"
        self.basepath = basepath
        self.timeout = timeout
        self._hosts = None

        # Setup orterun command defaults
        self.runner = Orterun(
            ServerCommand(os.path.join(basepath, "bin")), runnerpath, True)
        self.runner.enable_recovery.value = True
        if enable_path:
            self.runner.export.value = ["PATH"]

        # Setup server command defaults
        self.runner.job.attach.value = attach
        self.runner.job.debug.value = True
        self.runner.job.insecure.value = True
        self.runner.job.request.value = "start"
        self.runner.job.sudo = True

        # Parameters that user can specify in the test yaml to modify behavior.
        self.debug = BasicParameter(None)           # ServerCommand param
        self.insecure = BasicParameter(None)        # ServerCommand param
        self.sudo = BasicParameter(None)            # ServerCommand param
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

    def get_params(self, test):
        """Get values from the yaml file and assign them respectively
            to the server command and the orterun command.

        Args:
            test (Test): avocado Test object
        """
        server_params = ["attach", "debug", "insecure", "sudo"]
        runner_params = ["enable_recovery", "export"]
        super(ServerManager, self).get_params(test)
        for name in self.get_param_names():
            if name in server_params:
                getattr(
                    self.runner.job, name).value = getattr(self, name).value
            if name in runner_params:
                getattr(self.runner, name).value = getattr(self, name).value

    def set_server_yaml(self, log_file):
        """Set the server's config option to the path of the yaml file."""
        self.runner.job.config.value = self.runner.job.yaml.create_yaml(
            self.basepath, log_file)

    def check_subprocess_status(self, subprocess):
        """Wait for message from command output."""
        patterns = {
            "format": "SCM format required",
            "normal": "DAOS I/O server.*started",
        }
        start_time = time.time()
        start_msgs = 0
        timed_out = False
        while start_msgs != len(self._hosts) and not timed_out:
            output = subprocess.get_stdout()
            start_msgs = len(re.findall(patterns[self.mode], output))
            timed_out = time.time() - start_time > self.timeout

        if start_msgs != len(self._hosts):
            err_msg = "{} detected. Only {}/{} messages received".format(
                "Time out" if timed_out else "Error",
                start_msgs, len(self._hosts))
            print("{}:\n{}".format(err_msg, subprocess.get_stdout()))
            raise CommandFailure(err_msg)

        print("Started server in <{}> mode.".format(self.mode))
        return True

    def start(self, log_file=None):
        """Start the server through the runner."""

        self.set_server_yaml(log_file)

        if self.runner.job.yaml.is_nvme():
            self.mode = "format"
            storage_prepare(self._hosts)
            self.prep()

        try:
            self.runner.run()
        except CommandFailure as details:
            print("Failed to start server. ERROR: {}".format(details))

        if self.runner.job.yaml.is_nvme():
            storage_format(self._hosts)

        self.mode = "normal"
        try:
            self.check_subprocess_status(self.runner._process)
        except CommandFailure as details:
            print("Failed to start after format: {}".format(details))

        return True

    def stop(self):
        """Stop the server through the runner."""
        if self.runner.job.yaml.is_nvme():
            storage_reset(self._hosts)

        print("Stopping servers")
        error_list = []
        try:
            self.runner.stop()
        except CommandFailure as error:
            error_list.append("Error stopping servers: {}".format(error))

        return error_list

    def prep(self):
        """Prepare the hosts before starting daos server.

        Args:
            hosts (list): list of host names to prepare

        Raises:
            ServerFailed: if there is any errors preparing the hosts

        """
        # Kill any doas servers running on the hosts
        self.kill()

        # Clean up any files that exist on the hosts
        self.clean(scm=True)

        # Verify the domain socket directory is present and owned by this user
        user = getpass.getuser()
        file_checks = (
            ("Server", self._hosts, "/var/run/daos_server"),
        )
        for host_type, host_list, directory in file_checks:
            status, nodeset = check_file_exists(host_list, directory, user)
            if not status:
                raise ServerFailed(
                    "{}: {} missing directory {} for user {}.".format(
                        nodeset, host_type, directory, user))

    def kill(self):
        """Forcably kill any daos server processes running on hosts.

        Sometimes stop doesn't get everything.  Really whack everything
        with this.

        """
        kill_cmds = [
            "pkill '(daos_server|daos_io_server)' --signal INT",
            "sleep 5",
            "pkill '(daos_server|daos_io_server)' --signal KILL",
        ]
        # Intentionally ignoring the exit status of the command
        result = pcmd(self._hosts, "; ".join(kill_cmds), False, None, None)
        if len(result) > 1 or 0 not in result:
            raise ServerFailed(
                "Error cleaning tmpfs on servers: {}".format(
                    ", ".join(
                        [str(result[key]) for key in result if key != 0])))

    def clean(self, scm=False):
        """Clean the tmpfs on the servers.

        Args:
            scm (bool): if true, remove tmpfs mount
        """
        clean_cmds = [
            "find /mnt/daos -mindepth 1 -maxdepth 1 -print0 | xargs -0r rm -rf"
        ]

        if self.runner.job.yaml.is_scm() and scm:
            clean_cmds.append("sudo umount /mnt/daos; sudo rm -rf /mnt/daos")

        # Intentionally ignoring the exit status of the command
        result = pcmd(self._hosts, "; ".join(clean_cmds), False, None, None)
        if len(result) > 1 or 0 not in result:
            raise ServerFailed(
                "Error cleaning /mnt/daos on servers: {}".format(
                    ", ".join(
                        [str(result[key]) for key in result if key != 0])))


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
