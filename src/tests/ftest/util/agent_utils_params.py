"""
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

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


class DaosAgentCredentialConfig(YamlParameters):
    """Defines the daos_agent credential_config yaml parameters."""

    def __init__(self):
        """Initialize a DaosAgentCredentialConfig object."""
        super().__init__("/run/agent_config/credential_config/*", None, "credential_config")

        # daos_agent credential_config parameters:
        #   - pool_auth_enabled: <bool>, attach per-pool node certs to pool
        #       connect credentials. Requires transport security.
        #   - node_cert_dir: <str>, directory holding per-pool <uuid>.{crt,key}
        self.pool_auth_enabled = BasicParameter(None)
        self.node_cert_dir = BasicParameter(None)


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

        # Support specifying a user owned runtime directory
        default_runtime_dir = os.environ.get("DAOS_AGENT_DRPC_DIR", "/var/run/daos_agent")

        # daos_agent parameters:
        #   - runtime_dir: <str>, e.g. /var/run/daos_agent
        #       Use the given directory for creating Unix domain sockets
        #   - log_file: <str>, e.g. /var/log/daos/daos_agent.log
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
        #   - telemetry_port: <int>, e.g. 9192
        #        Enable Prometheus endpoint for client telemetry.
        #   - telemetry_enabled: <bool>, e.g. True
        #        Enable client telemetry for all client processes.
        #   - telemetry_retain: <str>, e.g. 5m
        #        Time to retain per-client telemetry data.
        #   - access_points: <list>, e.g.  ["hostname1:10001"]
        #       Hosts can be specified with or without port, default port below
        #       assumed if not specified. Defaults to the hostname of this node
        #       at port 10000 for local testing.
        self.runtime_dir = BasicParameter(None, default_runtime_dir)
        self.log_file = LogParameter(log_dir, None, "daos_agent.log")
        self.control_log_mask = BasicParameter(None, "debug")
        self.exclude_fabric_ifaces = BasicParameter(None)
        self.cache_expiration = BasicParameter(None)
        self.disable_caching = BasicParameter(None)
        self.telemetry_port = BasicParameter(None)
        self.telemetry_enabled = BasicParameter(None)
        self.telemetry_retain = BasicParameter(None)
        self.access_points = BasicParameter(None, ["localhost"])

        self.credential_config = DaosAgentCredentialConfig()

    def get_params(self, test):
        """Get values for the yaml parameters from the test yaml file.

        Args:
            test (Test): avocado Test object
        """
        super().get_params(test)
        self.credential_config.get_params(test)

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        yaml_data = super().get_yaml_data()
        cred_data = self.credential_config.get_yaml_data()
        if cred_data.get(self.credential_config.title):
            yaml_data.update(cred_data)
        return yaml_data

    def is_yaml_data_updated(self):
        """Determine if any of the yaml file parameters have been updated.

        Returns:
            bool: whether or not a yaml file parameter has been updated

        """
        return super().is_yaml_data_updated() or self.credential_config.is_yaml_data_updated()

    def reset_yaml_data_updated(self):
        """Reset each yaml file parameter updated state to False."""
        super().reset_yaml_data_updated()
        self.credential_config.reset_yaml_data_updated()

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
