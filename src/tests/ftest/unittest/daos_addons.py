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

from avocado.utils import process
from general_utils import get_file_path
from apricot import TestWithServers

class DaosAddons(TestWithServers):
    """
    Avocado class for daos_addons test.
    :avocado: recursive
    """

    def test_runner(self, testname):
        """
        Test runner function.
        Args:
            testname: unittest name.
        return:
            None
        """
        name = self.params.get("testname", '/run/Test/{0}/'
                               .format(testname))
        bin_path = get_file_path(name, "install/bin")

        cmd = ("{0}".format(bin_path[0]))
        return_code = process.system(cmd)
        if return_code is not 0:
            self.fail("{0} Daos Addons test failed with return code={1}.\n"
                      .format(testname, return_code))

    def test_daos_addons(self):
        """
        Test daos_addons_test unittest.
        :avocado: tags=unittest,daos_addons
        """
        self.test_runner("daos_addons")
