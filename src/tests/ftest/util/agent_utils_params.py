#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from command_utils_base import \
    BasicParameter, LogParameter, YamlParameters, TransportCredentials


class DaosAgentTransportCredentials(TransportCredentials):
    """Transport credentials listing certificates for secure communication."""

    def __init__(self, log_dir="/tmp"):
        """Initialize a TransportConfig object."""
        super(DaosAgentTransportCredentials, self).__init__(
            "/run/agent_config/transport_config/*", "transport_config", log_dir)

        # Additional daos_agent transport credential parameters:
        #   - server_name: <str>, e.g. "daos_server"
        #       Name of server accodring to its certificate [daos_agent only]
        #
        self.server_name = BasicParameter(None, None)
        self.cert = LogParameter(log_dir, None, "agent.crt")
        self.key = LogParameter(log_dir, None, "agent.key")


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
