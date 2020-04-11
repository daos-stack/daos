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


class DaosAgentTransportCredentials(TransportCredentials):
    """Transport credentials listing certificates for secure communication."""

    def __init__(self):
        """Initialize a TransportConfig object."""
        super(DaosAgentTransportCredentials, self).__init__(
            "/run/agent_config/transport_config/*", "transport_config")

        # Additional daos_agent transport credential parameters:
        #   - server_name: <str>, e.g. "daos_server"
        #       Name of server accodring to its certificate [daos_agent only]
        #
        self.server_name = BasicParameter(None, "daos_server")


class DaosAgentYamlParameters(YamlParameters):
    """Defines the daos_agent configuration yaml parameters."""

    def __init__(self, filename, common_yaml):
        """Initialize an DaosAgentYamlParameters object.

        Args:
            filename (str): yaml configuration file name
            common_yaml (YamlParameters): [description]
        """
        super(DaosAgentYamlParameters, self).__init__(
            "/run/agent_config/*", filename, None, common_yaml)

        # All log files should be placed in the same directory on each host to
        # enable easy log file archiving by launch.py
        log_dir = os.environ.get("DAOS_TEST_LOG_DIR", "/tmp")

        # daos_agent parameters:
        #   - runtime_dir: <str>, e.g. /var/run/daos_agent
        #       Use the given directory for creating unix domain sockets
        #   - log_file: <str>, e.g. /tmp/daos_agent.log
        #       Full path and name of the DAOS agent logfile.
        self.runtime_dir = BasicParameter(None, "/var/run/daos_agent")
        self.log_file = LogParameter(log_dir, None, "daos_agent.log")

    def update_log_file(self, name):
        """Update the log file name for the daos agent.

        If the log file name is set to None the log file parameter value will
        not be updated.

        Args:
            name (str): log file name
        """
        if name is not None:
            self.log_file.update(name, "agent_config.log_file")
