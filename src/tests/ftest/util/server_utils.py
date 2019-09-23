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
import yaml
import getpass

from general_utils import pcmd, check_file_exists, poll_pattern, get_file_path
from command_utils import DaosCommand, Orterun
from command_utils import FormattedParameter
from get_hosts_from_file import get_hosts_from_file

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
        if path == "":
            path = get_file_path("bin/daos_server")

        super(ServerCommand, self).__init__(
            "/run/daos_server/*", "daos_server", path)

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


class ServerManager(object):
    """Defines object to manage server functions and launch server command."""

    def __init__(self, basepath, attach="/tmp", insecure=True,
                 debug=True, uri_path=None, env=None,
                 sudo=True, log_filename=None):
        """Create a ServerManager object.

        Args:
        """
        self.basepath = basepath
        self.log_filename = log_filename
        self._hosts = None
        self._hostfile = None

        # Setup server command defaults
        self.server_command = ServerCommand()
        self.server_command.attach.value = attach
        self.server_command.debug.value = debug
        self.server_command.insecure.value = insecure
        self.server_command.request.value = "start"
        self.set_server_yaml()

        # Setup orterun command defaults
        self.runner = Orterun(
            self.server_command, get_file_path("opt/ompi/bin/orterun"), True)
        self.runner.enable_recovery.value = True
        self.runner.report_uri.value = uri_path
        self.runner.sudo = sudo
        if env is not None:
            self.runner.export.value = []
            self.runner.export.value.extend(env.get_list())

    @property
    def hosts(self):
        """Hosts attribute getter."""
        return self._hosts

    @hosts.setter
    def hosts(self, value):
        """Hosts attribute setter

        Args:
            value (list): list of hosts
        """
        self._hosts = value
        self.runner.processes.value = len(self._hosts)

    @property
    def hostfile(self):
        """Hostfile attribute setter."""
        return self._hostfile

    @hostfile.setter
    def hostfile(self, value):
        """Hostfile attribute setter.

        Args:
            value (str): path to hostfile.
        """
        self._hostfile = value
        self._hosts = get_hosts_from_file(value)
        self.runner.hostfile.value = value
        self.runner.processes.value = len(self._hosts)

    def set_server_yaml(self):
        """Set the server's config option to the path of the yaml file.

        Args:
            basepath (str): DAOS install basepath
            log_filename (str): Set a specific logfile name
        """
        self.server_command.config.value = self._create_yaml(
            self.basepath, self.log_filename)

    def prep(self):
        """Prepare the hosts before starting daos server.

        Args:
            hosts (list): list of host names to prepare

        Raises:
            ServerFailed: if there is any errors preparing the hosts

        """
        # Kill any doas servers running on the hosts
        self.kill(self.hosts)

        # Clean up any files that exist on the hosts
        self.clean(self.hosts, scm=True)

        # Verify the domain socket directory is present and owned by this user
        user = getpass.getuser()
        file_checks = (
            ("Server", self.hosts, "/var/run/daos_server"),
        )
        for host_type, host_list, directory in file_checks:
            status, nodeset = check_file_exists(host_list, directory, user)
            if not status:
                raise ServerFailed(
                    "{}: {} missing directory {} for user {}.".format(
                        nodeset, host_type, directory, user))

    def start(self, mode="normal"):
        """Start the server in normal mode or maintanence mode.

        Args:
            mode (str, optional): If "normal", will start server and wait
                for IO servers to be started. If maintanence mode selected,
                will wait for server to ask for storage format and return
                when it does. Defaults to "normal".
        """
        patterns = {
            "mtnc": "needs format (mountpoint doesn't exist)",
            "normal": "DAOS I/O server.*started",
        }
        # Run servers
        result = self.runner.run()

        # Check for pattern
        if mode in patterns:
            pattern = patterns[mode]
            poll_pattern(len(self.hosts), result, pattern)

    def kill(self, hosts):
        # pylint: disable=pylint-no-self-use
        """Forcably kill any daos server processes running on hosts.

        Sometimes stop doesn't get everything.  Really whack everything
        with this.

        Args:
            hosts (list): list of host names where servers are running
        """
        kill_cmds = [
            "pkill '(daos_server|daos_io_server)' --signal INT",
            "sleep 5",
            "pkill '(daos_server|daos_io_server)' --signal KILL",
        ]
        # Intentionally ignoring the exit status of the command
        result = pcmd(hosts, "; ".join(kill_cmds), False, None, None)
        if len(result) > 1 or 0 not in result:
            raise ServerFailed(
                "Error cleaning tmpfs on servers: {}".format(
                    ", ".join(
                        [str(result[key]) for key in result if key != 0])))

    def clean(self, hosts, scm=False):
        # pylint: disable=pylint-no-self-use
        """Clean the tmpfs  on the servers.

        Args:
            hosts (list): list of host names where servers are running
            scm (bool): if true, remove tmpfs mount
        """
        clean_cmds = [
            "find /mnt/daos -mindepth 1 -maxdepth 1 -print0 | xargs -0r rm -rf"
        ]

        if scm:
            clean_cmds.append("sudo umount /mnt/daos; sudo rm -rf /mnt/daos")

        # Intentionally ignoring the exit status of the command
        result = pcmd(hosts, "; ".join(clean_cmds), False, None, None)
        if len(result) > 1 or 0 not in result:
            raise ServerFailed(
                "Error cleaning tmpfs on servers: {}".format(
                    ", ".join(
                        [str(result[key]) for key in result if key != 0])))

    def _create_yaml(self, basepath, log_filename):
        # pylint: disable=pylint-no-self-use
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
            traceback.print_exception(err.__class__, err, sys.exc_info()[2])
            raise ServerFailed(
                "Failed to Read {}/{}".format(basepath, DEFAULT_FILE))

        # Read the values from avocado_testcase.yaml file if test ran
        # with Avocado.
        new_value_set = {}
        if "AVOCADO_TEST_DATADIR" in os.environ:
            avocado_yaml_file = str(os.environ["AVOCADO_TEST_DATADIR"]).\
                                    split(".")[0] + ".yaml"

            # Read avocado test yaml file.
            try:
                with open(avocado_yaml_file, 'r') as rfile:
                    filedata = rfile.read()
                # Remove !mux for yaml load
                new_value_set = yaml.safe_load(filedata.replace('!mux', ''))
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

        # if sepcific log file name specified use that
        if log_filename:
            default_value_set['servers'][0]['log_file'] = log_filename

        # Write default_value_set dictionary in to AVOCADO_FILE
        # This will be used to start with daos_server -o option.
        try:
            with open('{}/{}'.format(basepath, AVOCADO_FILE), 'w') as wfile:
                yaml.dump(default_value_set, wfile, default_flow_style=False)
        except Exception as err:
            print("<SERVER> Exception occurred: {0}".format(str(err)))
            traceback.print_exception(err.__class__, err, sys.exc_info()[2])
            raise ServerFailed("Failed to Write {}/{}".format(basepath,
                                                              AVOCADO_FILE))

        return os.path.join(basepath, AVOCADO_FILE)
