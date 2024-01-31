"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from daos_io_conf import IoConfTestBase


class RbldRunIoConf(IoConfTestBase):
    """Test daos_run_io_conf.

    :avocado: recursive
    """

    def test_rebuild_run_io_conf(self):
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

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=rebuild,iorebuild
        :avocado: tags=RbldRunIoConf,test_rebuild_run_io_conf
        """
        self.execute_io_conf_run_test()
