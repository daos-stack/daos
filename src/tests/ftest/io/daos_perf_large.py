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

from daos_perf import DaosPerf

class DaosPerfLarge(DaosPerf):
    """
    Tests daos_perf with different config.
    :avocado: recursive
    """

    def test_large(self):
        """
        Jira ID: DAOS-1714
        Test Description: Large daos_perf test for performance purpose.
        Use Case: Run daos_perf for scm and nvme.
                  Run daos_perf for single and multiple number of objects.
                  Run daos_perf with 'LARGE' and 'R2S' object class.
                  Run the combination of above test cases with large number
                  of clients on four servers.
        :avocado: tags=daosperf,daosperflarge
        """
        # set nvme size
        pool_size_nvme = self.params.get(
            "size", '/run/daos_perf_large/pool_size/nvme/')
        self.daos_perf_cmd.pool_size_nvme.value = pool_size_nvme
        # run test
        self.runner("daos_perf_large")
