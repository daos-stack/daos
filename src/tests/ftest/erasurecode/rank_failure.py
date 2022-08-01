#!/usr/bin/python
"""
  (C) Copyright 2021-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from daos_io_conf import IoConfTestBase

class EcodRunIoConf(IoConfTestBase):
    """Test daos_run_io_conf with EC object class.

    :avocado: recursive
    """
    # pylint: disable=too-many-ancestors

    def test_daos_run_io_conf(self):
        """Jira ID: DAOS-7344.

        Test Description:
            daos_gen_io_conf bin utility used to create the data set based on input parameter.
            daos_run_io_conf will execute the data set. Utility will create mixed of single/array
            data set.It takes the snapshot of the record to verify the data later. During the test
            it will exclude/add the specific/all targets from specific rank. This verify the
            rebuild operation in loop for different targets and also verify the data content.

        Use Cases:
            Verify EC with data verification when target or ranks being excluded and added back.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_array,ec_fault
        :avocado: tags=ec_io_conf_run
        """
        self.execute_io_conf_run_test()
