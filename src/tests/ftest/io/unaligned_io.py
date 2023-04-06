"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from daos_io_conf import IoConfTestBase


class DaosRunIoConf(IoConfTestBase):
    """Test daos_run_io_conf.

    :avocado: recursive
    """
    def test_unaligned_io(self):
        """Jira ID: DAOS-3151.

        Test Description:
            Create the records with requested sizes in yaml.daos_run_io_conf
            will write the full data set. Modify single byte in random offset
            with different value. later verify the full data set where single
            byte will have only updated value, rest all data is intact with
            original value.

        Use Cases:
            Write data set, modified 1bytes in different offsets. Verify
            read through

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=io
        :avocado: tags=DaosRunIoConf,test_unaligned_io
        """
        self.unaligned_io()
