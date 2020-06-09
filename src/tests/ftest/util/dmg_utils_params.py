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

from command_utils_base import \
    BasicParameter, YamlParameters, TransportCredentials


class DmgTransportCredentials(TransportCredentials):
    """Transport credentials listing certificates for secure communication."""

    def __init__(self):
        """Initialize a TransportConfig object."""
        super(DmgTransportCredentials, self).__init__(
##DH            "/run/dmg/transport_config/*", "transport_config")
            "/run/transport_config/*", "transport_config")
##DH++ only dmg using the 1st "False"
        self.allow_insecure = BasicParameter(False, False)
        self.ca_cert = BasicParameter(None, "./daosCA/certs/daosCA.crt")
        self.cert = BasicParameter(None, "./daosCA/certs/admin.crt")
        self.key = BasicParameter(None, "./daosCA/certs/admin.key")


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
        #       Default port number with whith to bind the daos_server. This
        #       will also be used when connecting to access points if the list
        #       only contains host names.
        #
        self.name = BasicParameter(None, name)
        self.hostlist = BasicParameter(None, "localhost")
        self.port = BasicParameter(None, 10001)
