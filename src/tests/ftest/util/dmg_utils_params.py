"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from command_utils_base import (BasicParameter, LogParameter, TelemetryCredentials,
                                TransportCredentials, YamlParameters)


class DmgTransportCredentials(TransportCredentials):
    """Transport credentials listing certificates for secure communication."""

    def __init__(self, log_dir="/tmp"):
        """Initialize a TransportConfig object."""
        super().__init__("/run/dmg/transport_config/*", "transport_config", log_dir)
        self.cert = LogParameter(self._log_dir, None, "admin.crt")
        self.key = LogParameter(self._log_dir, None, "admin.key")

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            DmgTransportCredentials: a new DmgTransportCredentials object
        """
        return DmgTransportCredentials(self._log_dir)


class DmgTelemetryCredentials(TelemetryCredentials):
    """Telemetry credentials listing certificates for secure communication."""

    def __init__(self, log_dir="/tmp"):
        """Initialize a TelemetryCredentials object."""
        super().__init__("/run/dmg/telemetry_config/*", None, log_dir)
        self.ca_cert = LogParameter(self._log_dir, None, "daosTelemetryCA.crt")

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            DmgTelemetryCredentials: a new DmgTelemetryCredentials object
        """
        return DmgTelemetryCredentials(self._log_dir)


class DmgYamlParameters(YamlParameters):
    """Defines the dmg configuration yaml parameters."""

    def __init__(self, filename, name, transport, telemetry=None):
        """Initialize a DmgYamlParameters object.

        Args:
            filename (str): yaml configuration file name
            name (str): The DAOS system name.
            transport (DmgTransportCredentials): dmg security
                configuration settings.
            telemetry (DmgTelemetryCredentials): dmg telemetry
                configuration settings.
        """
        super().__init__("/run/dmg/*", filename, None, transport)

        # dmg parameters:
        #   - name: <str>, e.g. "daos_server"
        #       Name associated with the DAOS system.
        #
        #   - hostlist: <list>, e.g.  ["hostname1:10001"]
        #       Hosts can be specified with or without port, default port below
        #       assumed if not specified. Defaults to the hostname of this node
        #       at port 10001 for local testing
        #
        #   - port: <int>, e.g. 10001
        #       Default port number with with to bind the daos_server. This
        #       will also be used when connecting to access points if the list
        #       only contains host names.
        #
        self.name = BasicParameter(None, name)
        self.hostlist = BasicParameter(None, "localhost")
        self.port = BasicParameter(None, 10001)

        if telemetry is not None:
            self.telemetry_config = telemetry

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            DmgYamlParameters: a new DmgYamlParameters object
        """
        return DmgYamlParameters(self.filename, self.name.value, None)
