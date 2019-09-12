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
import resource
import signal
import fcntl
import errno
import yaml
import getpass

from general_utils import pcmd, check_file_exists
from command_utils import CommandWithParameters
from command_utils import BasicParameter, FormattedParameter
from avocado.utils import genio, process
from write_host_file import write_host_file

SESSIONS = {}

DEFAULT_FILE = "src/tests/ftest/data/daos_server_baseline.yaml"
AVOCADO_FILE = "src/tests/ftest/data/daos_avocado_test.yaml"


class ServerFailed(Exception):
    """Server didn't start/stop properly."""


class ServerCommand(DaosCommand):
    """Defines a object representing a server command."""

    def __init__(self, path):
        """Create a server Command object"""
        super(ServerCommand, self).__init__(
            "daos_server", "/run/daos_server/*", path)

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

    def __init__(self, test):
        """Create a ServerManager object"""
        self.test = test
        self.server = ServerCommand(
            os.path.join(self.test.prefix, "bin"))
        self.orterun = OrterunCommand(
            os.path.join(self.test.ompi_prefix, "bin"))

    def create_hostfile(self, slots):
        # This function will create a hostfile with the values from
        # self.hostlist.value
        # This should replace write_host_file???

    def update_configuration(self):
        """Update the server config parameter with a yaml file."""
        self.server.config.update(
            create_server_yaml(self.test.basepath), "server.config")

    def setup(self):
        """Setup server and job manager default attributes."""
        self.server.debug.update(True, "server.debug")
        self.server.insecure.update(True, "server.insecure")
        self.server.get_params(self.test)

        self.orterun.processes.update(True, "orterun.processes")
        self.orterun.debug.update(True, "orterun.processes")
        self.orterun.enable_recovery.update(True, "orterun.enable_recovery")
        self.orterun.get_params(self.test)

    def prepare(self, path, slots):
        """Prepare the hosts before starting daos server.

        Args:
            path (str): location to write the hostfile
            slots (int): slots per host to use in the hostfile

        Raises:
            ServerFailed: if there is any errors preparing the hosts

        """
        # Kill any doas servers running on the hosts
        kill_server(self.test.hostlist_servers)

        # Clean up any files that exist on the hosts
        clean_server(self.test.hostlist_servers)

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

        # Create the hostfile
        self.hostfile = write_host_file(self.hosts, path, slots)

    def kill_server(self, hosts):
        """Forcably kill any daos server processes running on the specified hosts.

        Sometimes stop doesn't get everything.  Really whack everything with this.

        Args:
            hosts (list): list of host names where servers are running
        """
        k_cmds = [
            "pkill '(daos_server|daos_io_server)' --signal INT",
            "sleep 5",
            "pkill '(daos_server|daos_io_server)' --signal KILL",
        ]
        # Intentionally ignoring the exit status of the command
        pcmd(
            self.test.hostlist_servers, "; ".join(k_cmds), False, None, None)

    def clean_server(self, hosts):
        """Clean the tmpfs  on the servers.

        Args:
            hosts (list): list of host names where servers are running
        """
        c_cmds = [
            "find /mnt/daos -mindepth 1 -maxdepth 1 -print0 | xargs -0r rm -rf"
        ]
        # Intentionally ignoring the exit status of the command
        pcmd(
            self.test.hostlist_servers, "; ".join(c_cmds), False, None, None)

    def set_nvme_mode(default_value_set, bdev, enabled=False):
        """Enable/Disable NVMe Mode.

        NVMe is enabled by default in yaml file.So disable it for CI runs.

        Args:
            default_value_set (dict): dictionary of default values
            bdev (str): block device name
            enabled (bool, optional): enable NVMe. Defaults to False.
        """
        if 'bdev_class' in default_value_set['servers'][0]:
            if (default_value_set['servers'][0]['bdev_class'] == bdev and
                    not enabled):
                del default_value_set['servers'][0]['bdev_class']
        if enabled:
            default_value_set['servers'][0]['bdev_class'] = bdev


    def create_server_yaml(self, basepath):
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
            with open('{}/{}'.format(basepath, DEFAULT_FILE), 'r') as read_file:
                default_value_set = yaml.safe_load(read_file)
        except Exception as excpn:
            print("<SERVER> Exception occurred: {0}".format(str(excpn)))
            traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
            raise ServerFailed(
                "Failed to Read {}/{}".format(basepath, DEFAULT_FILE))

        # Read the values from avocado_testcase.yaml file if test ran with Avocado.
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
            except Exception as excpn:
                print("<SERVER> Exception occurred: {0}".format(str(excpn)))
                traceback.print_exception(
                    excpn.__class__, excpn, sys.exc_info()[2])
                raise ServerFailed(
                    "Failed to Read {}".format('{}.tmp'.format(avocado_yaml_file)))

        # Update values from avocado_testcase.yaml in DAOS yaml variables.
        if new_value_set:
            for key in new_value_set['server_config']:
                if key in default_value_set['servers'][0]:
                    default_value_set['servers'][0][key] = \
                        new_value_set['server_config'][key]
                elif key in default_value_set:
                    default_value_set[key] = new_value_set['server_config'][key]

        # Disable NVMe from baseline data/daos_server_baseline.yml
        set_nvme_mode(default_value_set, "nvme")

        # Write default_value_set dictionary in to AVOCADO_FILE
        # This will be used to start with daos_server -o option.
        try:
            with open('{}/{}'.format(basepath,
                                    AVOCADO_FILE), 'w') as write_file:
                yaml.dump(default_value_set, write_file, default_flow_style=False)
        except Exception as excpn:
            print("<SERVER> Exception occurred: {0}".format(str(excpn)))
            traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
            raise ServerFailed("Failed to Write {}/{}".format(basepath,
                                                            AVOCADO_FILE))

        return os.path.join(basepath, AVOCADO_FILE)
