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
from apricot import skipForTicket

from general_utils import convert_list
from macsio_test_base import MacsioTestBase


class MacsioTest(MacsioTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs a basic MACSio test.

    :avocado: recursive
    """

    @skipForTicket("DAOS-5265")
    def test_macsio(self):
        """JIRA ID: DAOS-3658.

        Test Description:
            Purpose of this test is to check basic functionality for DAOS,
            MPICH, HDF5, and MACSio.

        Use case:
            Six client and two servers.

        :avocado: tags=all,pr,hw,large,io,macsio
        """
        # Create a pool
        self.add_pool()
        self.pool.display_pool_daos_space()

        # Create a container
        self.add_container(self.pool)

        # Run macsio
        self.log.info("Running MACSio")
        status = self.macsio.check_results(
            self.run_macsio(
                self.pool.uuid, convert_list(self.pool.svc_ranks),
                self.container.uuid))
        if status:
            self.log.info("Test passed")
