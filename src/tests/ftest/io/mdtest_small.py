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
        :avocado: tags=all,pr,hw,mdtest,mdtestsmall
        """
        # local params
        mdtest_flags = self.params.get("mdtest_flags", "/run/mdtest/*")
        mdtest_api = self.params.get("mdtest_api", "/run/mdtest/*")
        read_write = self.params.get("read_write", "/run/mdtest/*")
        branch_factor = self.params.get("branch_factor", "/run/mdtest/*")
        dir_depth = self.params.get("dir_depth", "/run/mdtest/*")
        num_of_items = self.params.get("num_of_items", "/run/mdtest/*")
        number_of_files_dirs = self.params.get("number_of_files_dirs",
                                               "/run/mdtest/*")

        # Running mdtest for different variants
        for flag in mdtest_flags:
            self.mdtest_cmd.flags.update(flag)
            if self.mdtest_cmd.flags.value == ' ':
                for api in mdtest_api:
                    self.mdtest_cmd.api.update(api)
                    if self.mdtest_cmd.api.value == 'DFS':
                        self.mdtest_cmd.write_bytes.update(read_write[1])
                        self.mdtest_cmd.read_bytes.update(read_write[1])
                    else:
                        self.mdtest_cmd.write_bytes.update(read_write[0])
                        self.mdtest_cmd.read_bytes.update(read_write[0])
                    for branch in branch_factor:
                        self.mdtest_cmd.branching_factor.update(branch)
                        if (self.mdtest_cmd.branching_factor.value ==
                                branch_factor[0]):
                            self.mdtest_cmd.num_of_files_dirs.update(
                                number_of_files_dirs)
                            for depth in dir_depth[:-1]:
                                self.mdtest_cmd.depth.update(depth)
                                self.execute_mdtest()
                                self.mdtest_cmd.num_of_files_dirs.update(" ")
                        else:
                            self.mdtest_cmd.items.update(num_of_items)
                            self.mdtest_cmd.depth.update(dir_depth[2])
                            self.execute_mdtest()
                            self.mdtest_cmd.items.update(" ")
            else:
                for api in mdtest_api:
                    self.mdtest_cmd.api.update(api)
                    if self.mdtest_cmd.api.value == 'POSIX':
                        self.mdtest_cmd.write_bytes.update(read_write[1])
                        self.mdtest_cmd.read_bytes.update(read_write[1])
                        self.mdtest_cmd.branching_factor.update(
                            branch_factor[0])
                        self.mdtest_cmd.num_of_files_dirs.update(
                            number_of_files_dirs)
                        self.mdtest_cmd.depth.update(dir_depth[1])
                    else:
                        self.mdtest_cmd.write_bytes.update(read_write[0])
                        self.mdtest_cmd.read_bytes.update(read_write[0])
                        self.mdtest_cmd.branching_factor.update(
                            branch_factor[1])
                        self.mdtest_cmd.items.update(num_of_items)
                        self.mdtest_cmd.depth.update(dir_depth[2])

                    self.execute_mdtest()
                    self.mdtest_cmd.num_of_files_dirs.update(" ")
                    self.mdtest_cmd.items.update(" ")
