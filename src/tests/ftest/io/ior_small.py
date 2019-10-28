#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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

from ior_test_base import IorTestBase


class IorSmall(IorTestBase):
    """Test class Description: Runs IOR with 1 server with basic parameters.

    :avocado: recursive
    """

    def test_ior_small(self):
        """Jira ID: DAOS-2715.

        Test Description:
            Purpose of this test is to have small ior test to check basic
            functionality for both DAOS and MPIIO api

        Use case:
            Run ior with read, write, CheckWrite, CheckRead in ssf mode.
            Run ior with read, write, CheckWrite, CheckRead in fpp mode.
            Run ior with read, write, CheckWrite and access to random
                offset instead of sequential.
            All above three cases to be run with single client and
                multiple client processes in two separate nodes.

        :avocado: tags=all,daosio,small,pr,hw,iorsmall
        """
        self.run_ior_with_pool()
