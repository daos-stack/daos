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
import time
import subprocess
import json
import re
import signal
import errno
import yaml
import getpass

from general_utils import pcmd, check_file_exists
from command_utils import JobManager, DaosCommand, Orterun
from command_utils import BasicParameter, FormattedParameter
from avocado.utils import genio, process
from write_host_file import write_host_file

SESSIONS = {}

DEFAULT_FILE = "src/tests/ftest/data/daos_server_baseline.yaml"
AVOCADO_FILE = "src/tests/ftest/data/daos_avocado_test.yaml"


class ServerFailed(Exception):
    """Server didn't start/stop properly."""


class ServerCommand(DaosCommand):
    """Defines an object representing a server command."""

    def __init__(self, path):
        """Create a server command object

        Args:
            path (str): path to location of daos_server binary.
        """
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

    def __init__(self, hosts, path="", manager_path=""):
        """Create a ServerManager object.

        Args:
            hosts (list): list of servers to launch/setup.
            path (str, optional): path to location of daos_server binary.
                Defaults to "".
            manager_path (str, optional): path to location of orterun binary.
                Defaults to "".
        """
        self.hosts = hosts
        self.cmd = ServerCommand(path)
        self.orterun = Orterun(self.cmd, manager_path, True)

    def setup(self, env=None, basepath="", hostfile="", test=None):
        """Setup server and orterun default attributes.

        Args:
            env (EnvironmentVariables, optional): the environment variables
                to use with the orterun command. Defaults to None.
            basepath (str, optional): DAOS install basepath.
                Defaults to "".
            hostfile (str, optional): file defining host names and slots.
                Defaults to "".
            test (Avocado Test Object, optional): If specified, used to
                override defaults with values from yaml file.
                Defaults to None.
        """
        self.cmd.debug.value = True
        self.cmd.insecure.value = True
        self.cmd.request.value = True
        self.cmd.config.value = self.create_server_yaml(basepath, None)

        self.orterun.setup_command(env, hostfile, len(self.hosts))
        self.orterun.enable_recovery.update(True, "orterun.enable_recovery")

        # If test object provided, override values with yaml file values.
        if test is not None:
            self.cmd.get_params(test)
            self.orterun.get_params(test)

        # Prepare servers
        self.prep()

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

    def kill(self, hosts):
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

    def create_server_yaml(self, basepath, log_filename):
        """Create the DAOS server config YAML file based on Avocado test
            Yaml file.

        Args:
            basepath (str): DAOS install basepath

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
