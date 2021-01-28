#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from command_utils_base import \
    BasicParameter, LogParameter, YamlParameters, TransportCredentials


class DmgTransportCredentials(TransportCredentials):
    """Transport credentials listing certificates for secure communication."""

    def __init__(self, log_dir="/tmp"):
        """Initialize a TransportConfig object."""
        super(DmgTransportCredentials, self).__init__(
            "/run/dmg/transport_config/*", "transport_config", log_dir)
        self.cert = LogParameter(log_dir, None, "admin.crt")
        self.key = LogParameter(log_dir, None, "admin.key")


class DmgYamlParameters(YamlParameters):
    """Defines the dmg configuration yaml parameters."""

    def __init__(self, filename, name, transport):
        """Initialize a DmgYamlParameters object.

        Args:
            filename (str): yaml configuration file name
            name (str): The DAOS system name.
            transport (DmgTransportCredentials): dmg security
                configuration settings.
        """
        super(DmgYamlParameters, self).__init__(
            "/run/dmg/*", filename, None, transport)

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
