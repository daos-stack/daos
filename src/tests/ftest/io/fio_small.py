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

from fio_test_base import FioBase


class FioSmall(FioBase):
    # pylint: disable=too-many-ancestors
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

        :avocado: tags=all,daily_regression,hw,medium,ib2,fio,fiosmall
        """
        self.execute_fio()
