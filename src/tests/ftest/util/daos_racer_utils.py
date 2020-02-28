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
from command_utils import \
    CommandFailure, FormattedParameter, ExecutableCommand
from general_utils import pcmd


class DaosRacerCommand(ExecutableCommand):
    """Defines a object representing a dfuse command."""

    def __init__(self, host):
        """Create a dfuse Command object."""
        super(DaosRacerCommand, self).__init__("/run/daos_racer", "daos_racer")

        # Number of seconds to run
        self.runtime = FormattedParameter("-t {}", 60)
        self.host = host

    def run(self):
        """Run the daos_racer command remotely.

        Raises:
            CommandFailure: if there is an error running the command

        """
        timeout = self.runtime.value + 15
        return_codes = pcmd([self.host], self.__str__(), True, timeout)
        if 0 not in return_codes or len(return_codes) > 1:
            raise CommandFailure("Error running '{}'".format(self.__str__()))
