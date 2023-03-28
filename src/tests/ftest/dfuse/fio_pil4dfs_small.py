"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from fio_test_base import FioBase
from fio_utils import FioCommand

class FioILSmall(FioBase):
    """Test class Description: Runs Fio with in small config.

    :avocado: recursive
    """

    def setUp(self, il_lib_path=None):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super().setUp()

        # Get the parameters for Fio
        self.fio_cmd = FioCommand(il_lib_path='/usr/lib64/libpil4dfs.so')
        self.fio_cmd.get_params(self)
        self.processes = self.params.get("np", '/run/fio/client_processes/*')
        self.manager = self.params.get("manager", '/run/fio/*', "MPICH")

    def test_fio_small_pil4dfs(self):
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
        :avocado: tags=dfuse,fio,checksum,tx
        :avocado: tags=FioSmall,test_fio_small,test_fio_pil4dfs_small
        """
        self.execute_fio()
