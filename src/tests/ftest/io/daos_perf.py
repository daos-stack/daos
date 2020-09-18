#!/usr/bin/python
"""
  (C) Copyright 2019-2020 Intel Corporation.

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
from daos_perf_base import DaosPerfBase


class DaosPerf(DaosPerfBase):
    # pylint: disable=too-many-ancestors
    """Test cases for the daos_perf command.

    Test Class Description:
        Tests daos_perf with different configurations.

    :avocado: recursive
    """

    def test_small(self):
        """Jira ID: DAOS-1714.

        Test Description:
            Small daos_perf test

        Use cases:
            Run daos_perf in 'daos' and 'vos' modes.  Run daos_perf using single
            value and array value types for 'vos' mode. Also run the above
            config with and without shuffle option '-S' of daos_perf.  Run
            daos_perf using single value type for 'LARGE' and 'R2s' object
            class. Run this config with multiple server/client configuration.

        :avocado: tags=daosperf,daosperfsmall
        """
        self.run_daos_perf()
