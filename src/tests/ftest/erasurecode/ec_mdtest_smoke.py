#!/usr/bin/python
'''
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
'''
from mdtest_test_base import MdtestBase

class ErasureCodeMdtest(MdtestBase):
    # pylint: disable=too-many-ancestors
    """
    EC MDtest class to run smoke tests.
    :avocado: recursive
    """

    def test_mdtest_large(self):
        """
        Jira ID: DAOS-2494
        Test Description:
            Test EC object class with.
        Use Cases:
            Create the pool and run EC object class till 8P2.

        :avocado: tags=all,pr,hw,large,ec,ec_smoke,ec_mdtest
        """
        mdtest_flags = self.params.get("flags", "/run/mdtest/*")
        self.mdtest_cmd.flags.update(mdtest_flags)

        obj_class = self.params.get("dfs_oclass", '/run/mdtest/objectclass/*')
        for oclass in obj_class:
            self.mdtest_cmd.dfs_oclass.update(oclass)
            self.mdtest_cmd.dfs_dir_oclass.update(oclass)
            self.execute_mdtest()
