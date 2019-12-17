#!/usr/bin/python
'''
  (C) Copyright 2019 Intel Corporation.

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
'''
from __future__ import print_function

from apricot import TestWithServers, skipForTicket
from mpio_utils import MpioUtils, MpioFailed


class Romio(TestWithServers):
    """Runs Romio test.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize the Romio object."""
        super(Romio, self).__init__(*args, **kwargs)
        self.mpio = None

    @skipForTicket("CORCI-635")
    def test_romio(self):
        """Test ID: DAOS-1994.

        Run Romio test provided in mpich package
        Testing various I/O functions provided in romio test suite

        :avocado: tags=all,mpiio,pr,small,romio
        """
        # setting romio parameters
        romio_test_repo = self.params.get("romio_repo", '/run/romio/')

        # initialize MpioUtils
        self.mpio = MpioUtils()
        if self.mpio.mpich_installed(self.hostlist_clients) is False:
            self.fail("Exiting Test: Mpich not installed")

        try:
            # Run romio
            self.mpio.run_romio(
                self.basepath, self.hostlist_clients, romio_test_repo)

            # Parsing output to look for failures
            # stderr directed to stdout
            stdout = self.logdir + "/stdout"
            searchfile = open(stdout, "r")
            error_message = ["non-zero exit code", "MPI_Abort", "errors",
                             "failed to create pool",
                             "failed to parse pool UUID",
                             "failed to destroy pool"]

            for line in searchfile:
                for error_msg in error_message:
                    if error_msg in line:
                        self.fail(
                            "Romio Test Failed with error_message: {}".format(
                                error_msg))

        except (MpioFailed) as excep:
            self.fail("<Romio Test Failed> \n{}".format(excep))
