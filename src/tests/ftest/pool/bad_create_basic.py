#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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


class BadCreateTest(PoolTestBase):
    """Test pool create calls.

    Test Class Description:
        Tests pool create API by passing NULL and otherwise inappropriate
        parameters.  This can't be done with daosctl, need to use the python
        API.

    :avocado: recursive
    """

    def test_create_basic(self):
        """Test ID: DAOS-???.

        Test Description:
            The DaosPool.create() method converts some of the inputs into
            c_types before calling the C function.  To reduce the number of
            permutations test the null values for theses arguments separately
            to verify the basic invalid argument handling of the python code.

        :avocado: tags=all,pool,full_regression,tiny,badcreate,basic
        """
        namespace = self.params.get("pool_namespace", "/run/pool_namespaces/*")
        self.create_pool_test(namespace)
