"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from command_utils_base import BasicParameter, LogParameter, TransportCredentials, YamlParameters


class DaosAgentTransportCredentials(TransportCredentials):
    """Transport credentials listing certificates for secure communication."""

    def __init__(self, log_dir="/tmp"):
        """Initialize a TransportConfig object."""
        super().__init__("/run/agent_config/transport_config/*", "transport_config", log_dir)

        # Additional daos_agent transport credential parameters:
        #   - server_name: <str>, e.g. "daos_server"
        #       Name of server according to its certificate [daos_agent only]
        #
        self.server_name = BasicParameter(None, None)
        self.cert = LogParameter(self._log_dir, None, "agent.crt")
        self.key = LogParameter(self._log_dir, None, "agent.key")

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            DaosAgentTransportCredentials: a new DaosAgentTransportCredentials object
        """
        return DaosAgentTransportCredentials(self._log_dir)


class DaosAgentYamlParameters(YamlParameters):
    """Defines the daos_agent configuration yaml parameters."""

    def __init__(self, filename, common_yaml):
        """Initialize an DaosAgentYamlParameters object.

        Args:
            filename (str): yaml configuration file name
            common_yaml (YamlParameters): yaml configuration common to daos servers and agents
        """
        super().__init__("/run/agent_config/*", filename, None, common_yaml)

        # All log files should be placed in the same directory on each host to
        # enable easy log file archiving by launch.py
        log_dir = os.environ.get("DAOS_TEST_LOG_DIR", "/tmp")

        # daos_agent parameters:
        #   - runtime_dir: <str>, e.g. /var/run/daos_agent
        #       Use the given directory for creating Unix domain sockets
        #   - log_file: <str>, e.g. /tmp/daos_agent.log
        #       Full path and name of the DAOS agent logfile.
        #   - control_log_mask: <str>, one of: error, info, debug
        #       Specifies the log level for agent logs.
        #   - exclude_fabric_ifaces: <list>, Ignore a subset of fabric interfaces when selecting
        #       an interface for client applications.
        #   - cache_expiration: <int>, Time in minutes to expire agent's cache that will
        #       be refreshed the next time hardware data or engine rank connection information
        #       is requested. A value of 0 means the cache never expires.
        #   - disable_caching: <bool>, Whether to disable the agent's internal caches. If true,
        #       the agent will query the server access point and local hardware data every time
        #       a client requests rank connection information.
        self.runtime_dir = BasicParameter(None, "/var/run/daos_agent")
        self.log_file = LogParameter(log_dir, None, "daos_agent.log")
        self.control_log_mask = BasicParameter(None, "debug")
        self.exclude_fabric_ifaces = BasicParameter(None)
        self.cache_expiration = BasicParameter(None)
        self.disable_caching = BasicParameter(None)

    def update_log_file(self, name):
        """Update the log file name for the daos agent.

        If the log file name is set to None the log file parameter value will
        not be updated.

        Args:
            name (str): log file name
        """
        if name is not None:
            self.log_file.update(name, "agent_config.log_file")

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            DaosAgentYamlParameters: a new DaosAgentYamlParameters object
        """
        return DaosAgentYamlParameters(self.filename, None)
