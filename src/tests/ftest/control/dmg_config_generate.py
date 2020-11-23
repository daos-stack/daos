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

from general_utils import pcmd
from apricot import TestWithServers
from server_utils import ServerFailed
from command_utils_base import CommandFailure


class ConfigGenerate(TestWithServers):
    """Test Class Description:

    Verify the veracity of the configuration created by the commmand and what
    the user specified, input verification and correct execution of the server
    once the generated configuration is propagated to the servers.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ConfigGenerate object."""
        super(ConfigGenerate, self).__init__(*args, **kwargs)
        self.setup_start_servers = False

    def dmg_generate_config(self):
        """ Verify that dmg can generate an accurate configuration file."""

        # Let's create an empty config file on the server/s
        cfg_file = self.get_config_file("daos_server", "server_discover")
        pcmd(self.hostlist_servers, "touch {}".format(cfg_file))

        # Update the config value for the server to an empty config file.
        # Setup the server managers
        self.add_server_manager(config_file=cfg_file)
        self.configure_manager(
            "server", self.server_managers[-1], self.hostlist_servers,
            self.hostfile_servers_slots)

        # Start the server in discovery mode
        try:
            self.server_managers[0].detect_start_mode("discover")
        except ServerFailed as err:
            self.fail("Error starting server in discovery mode: {}".format(err))

        # Let's get the config file contents
        yaml_data = self.get_dmg_command().config_generate()

        # Stop server
        self.server_managers[-1].stop()

        # Setup and start the servers
        extra_servers = self.params.get("test_servers", "/run/extra_servers/*")
        self.log.info("Extra Servers = %s", extra_servers)
        self.hostlist_servers.extend(extra_servers)

        self.configure_manager(
            "server", self.server_managers[-1], self.hostlist_servers,
            self.hostfile_servers_slots)

        # Verify that all daos_io_server instances are started.
        try:
            self.server_managers[-1].start(config_data=yaml_data)
        except CommandFailure as err:
            self.fail("Error starting servers with dmg generated"
                      " config: {}".format(err))

