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

from command_utils import \
    CommandFailure, FormattedParameter, ExecutableCommand, EnvironmentVariables
from general_utils import pcmd


class DaosRacerCommand(ExecutableCommand):
    """Defines a object representing a daos_racer command."""

    def __init__(self, path, host):
        """Create a daos_racer command object.

        Args:
            path (str): path of the daos_racer command
            host (str): host on which to run the daos_racer command
        """
        super(DaosRacerCommand, self).__init__(
            "/run/daos_racer", "daos_racer", path)
        self.host = host

        # Number of seconds to run
        self.runtime = FormattedParameter("-t {}", 60)

    def run(self):
        """Run the daos_racer command remotely.

        Raises:
            CommandFailure: if there is an error running the command

        """
        # Include exports prior to the daos_racer command
        env = EnvironmentVariables()
        env["OMPI_MCA_btl_openib_warn_default_gid_prefix"] = "0"
        env["OMPI_MCA_btl"] = "tcp,self"
        env["OMPI_MCA_oob"] = "tcp"
        env["OMPI_MCA_pml"] = "ob1"
        env["OFI_INTERFACE"] = os.getenv("OFI_INTERFACE", "eth0")
        env["CRT_PHY_ADDR_STR"] = os.getenv("CRT_PHY_ADDR_STR", "ofi+sockets")
        self._pre_command = env.get_export_str()

        # The daos_racer program will run longer than the '-t <seconds>'
        # specified on its command line.  Use a timeout with pcmd to stop
        # testing if daos_racer does not complete in the intended timeframe.
        # Add a 30s buffer to this timeout to hopefully avoid timing issues.
        timeout = self.runtime.value + 30

        # Run daos_racer on the specified host
        self.log.info(
            "Running %s on %s with a %ss timeout",
            self.__str__(), self.host, timeout)
        return_codes = pcmd([self.host], self.__str__(), True, timeout)
        if 0 not in return_codes or len(return_codes) > 1:
            # Kill the daos_racer process if the remote command timed out
            if 255 in return_codes:
                self.log.info(
                    "Stopping timed out daos_racer process on %s", self.host)
                pcmd([self.host], "pkill daos_racer", True)

            raise CommandFailure("Error running '{}'".format(self._command))

        self.log.info("Test passed!")
