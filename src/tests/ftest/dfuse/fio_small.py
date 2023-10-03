"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from fio_test_base import FioBase


class FioSmall(FioBase):
    """Test class Description: Runs Fio with in small config.

    :avocado: recursive
    """

    def test_fio_small(self):
        """Jira ID: DAOS-2493.

        Test Description:
            Test Fio in small config.

        Use Cases:
            Aim of this test is to test different combinations
            of following fio configs:
            1 Client
            ioengine: 'libaio'
            thread: 1
            verify: 'crc64'
            iodepth: 16
            blocksize: 256B|1M
            size: 1M|1G
            read_write: rw|randrw
            numjobs: 1

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dfuse,fio,checksum,tx
        :avocado: tags=FioSmall,test_fio_small
        """
        self.execute_fio()
