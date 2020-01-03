#!/usr/bin/python
"""
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
"""
from pool_test_base import PoolTestBase


class BadCreate(PoolTestBase):
    # pylint: disable=too-many-ancestors
    """Test pool creation with NVMe hardware.

    Test Class Description:
        Tests pool create API by passing NULL and otherwise inappropriate
        parameters.  This can't be done with daosctl, need to use the python
        API.

    :avocado: recursive
    """

    def test_bad_create_hw(self):
        """Test ID: DAOS-???.

        Test Description:
            Pass bad parameters to pool create.

        :avocado: tags=all,pool,full_regression,hw,tiny,badcreate
        """
        self.create_pool_test()
