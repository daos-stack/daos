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


class DaosRunIoConf(IoConfTestBase):
    """Test daos_run_io_conf.

    :avocado: recursive
    """
    # pylint: disable=too-many-ancestors
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

        :avocado: tags=all,full_regression,hw,medium,ib2,unaligned_io
        """
        self.unaligned_io()
