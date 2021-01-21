#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

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
from ior_test_base import IorTestBase

class ErasureCodeIor(IorTestBase):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object type classes.
    :avocado: recursive
    """

    def test_ec(self):
        """Jira ID: DAOS-5812.

        Test Description: Test Erasure code object with IOR.
        Use Case: Create the medium size of pool and run IOR with supported
                  EC object type class for sanity purpose.

        :avocado: tags=all,pr,daily_regression,hw,large,ec,ec_smoke,ec_ior
        """
        obj_class = self.params.get("dfs_oclass", '/run/ior/objectclass/*')

        for oclass in obj_class:
            self.ior_cmd.dfs_oclass.update(oclass)
            self.ior_cmd.dfs_dir_oclass.update(oclass)
            self.run_ior_with_pool()
