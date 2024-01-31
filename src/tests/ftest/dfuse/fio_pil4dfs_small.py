"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from fio_test_base import FioBase


class FioPil4dfsSmall(FioBase):
    """Test class Description: Runs Fio with in small config.

    :avocado: recursive
    """

    def test_fio_pil4dfs_small(self):
        """Jira ID: DAOS-12142.

        Test Description:
            Test Fio in small config with libpil4dfs.so.

        Use Cases:
            Aim of this test is to test different combinations
            of following fio configs:
            1 Client
            thread: 1
            verify: 'crc64'
            iodepth: 16
            blocksize: 256B|1M
            size: 1M|1G
            read_write: rw|randrw
            numjobs: 1

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dfuse,fio,checksum,tx,pil4dfs
        :avocado: tags=FioPil4dfsSmall,test_fio_pil4dfs_small
        """
        self.fio_cmd.env['LD_PRELOAD'] = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
        self.execute_fio()
