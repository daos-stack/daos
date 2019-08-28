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

from mdtest_test_base import MdtestBase

class MdtestSmall(MdtestBase):
    """Test class Description: Runs Mdtest with in small config.
    :avocado: recursive
    """

    def test_mdtest_small(self):
        """Jira ID: DAOS-2493.
        Test Description:
            Test Mdtest in small config.
        Use Cases:
            Aim of this test is to test different combinations
            of following configs:
            1/8 Clients
            num of files/dirs: 100
            with/without unique working dir for each task
            write bytes: 0|4K
            read bytes: 0|4K
            depth of hierarchical directory structure: 0|5
        :avocado: tags=all,mdtest,mdtestsmall
        """
        mdtest_flags = self.params.get("flags", "/run/mdtest/*")
        self.mdtest_cmd.flags.update(mdtest_flags)
        self.execute_mdtest()
