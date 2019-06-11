#!/usr/bin/python
'''
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
'''


from avocado.utils import process
from general_utils import get_file_path
from apricot import Test

class UnitTest(Test):
    """
    Avocado Unit Test class.
    :avocado: recursive
    """
    def tearDown(self):
        process.system("rm -f /mnt/daos/*")

    def unittest_runner(self, unit_testname):
        """
        Common unitetest runner function.
        Args:
            unit_testname: unittest name.
        return:
            None
        """
        name = self.params.get("testname", '/run/UnitTest/{0}/'
                               .format(unit_testname))
        bin_path = get_file_path(name, "install/bin")

        cmd = ("{0}".format(bin_path[0]))
        return_code = process.system(cmd)
        if return_code is not 0:
            self.fail("{0} unittest failed with return code={1}.\n"
                      .format(unit_testname, return_code))

    def test_smd_ut(self):
        """
        Test smd unittest.
        :avocado: tags=unittest,nvme,smd_ut
        """
        self.unittest_runner("smd_ut")

    def test_vea_ut(self):
        """
        Test vea unittest.
        :avocado: tags=unittest,nvme,vea_ut
        """
        self.unittest_runner("vea_ut")

    def test_pl_map(self):
        """
        Test pl_map unittest.
        :avocado: tags=unittest,pl_map
        """
        self.unittest_runner("pl_map")

    def test_eq_tests(self):
        """
        Test eq_tests unittest.
        :avocado: tags=unittest,eq_tests
        """
        self.unittest_runner("eq_tests")

    def test_vos_tests(self):
        """
        Test eq_tests unittest.
        :avocado: tags=unittest,vos_tests
        """
        self.unittest_runner("vos_tests")
