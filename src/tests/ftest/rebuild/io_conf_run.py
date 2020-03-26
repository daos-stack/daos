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
from daos_io_conf import IoConfTestBase
from apricot import skipForTicket

class DaosRunIoConf(IoConfTestBase):
    """Test daos_run_io_conf.

    :avocado: recursive
    """
    # pylint: disable=too-many-ancestors

    @skipForTicket("DAOS-3866")
    def test_daos_run_io_conf(self):
        """Jira ID: DAOS-3150.

        Test Description:
            daos_gen_io_conf bin utility used to create the data set based on
            input parameter. daos_run_io_conf will execute the data set.
            Utility will create mixed of single/array data set.It takes the
            snapshot of the record to verify the data later. During the test
            it will exclude/add the specific/all targets from specific rank.
            This verify the rebuild operation in loop for different targets
            and also verify the data content.

        Use Cases:
            Verify rebuild with data verification.

        :avocado: tags=all,full_regression,hw,large,rebuild,iorebuild
        """
        self.execute_io_conf_run_test()
