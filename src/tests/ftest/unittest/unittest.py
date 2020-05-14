#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

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
from general_utils import get_file_path, run_command
from apricot import Test, skipForTicket


def unittest_runner(self, unit_testname):
    """Run a unit test.

    Unit tests needs to be run on local machine incase of server start required
    For other unit tests, which does not required to start server,it needs to be
    run on server where /mnt/daos mounted.

    Args:
        unit_testname (str): unittest name.
    """
    name = self.params.get("testname", '/run/UnitTest/{0}/'
                           .format(unit_testname))
    server = self.params.get("test_servers", "/run/hosts/*")
    bin_path = get_file_path(name, "install/bin")

    cmd = ("/usr/bin/ssh {} {}".format(server[0], bin_path[0]))

    result = run_command(cmd, raise_exception=False, output_check="both")

    if result.exit_status != 0:
        self.fail("{0} unittest failed with return code={1}.\n"
                  .format(unit_testname, result.exit_status))


class UnitTestWithoutServers(Test):
    """
    Test Class Description: Avocado Unit Test class for tests which don't
                            need servers.
    :avocado: recursive
    """

    def test_smd_ut(self):
        """
        Test Description: Test smd unittest.
        Use Case: This tests smd's following functions: nvme_list_streams,
                  nvme_get_pool, nvme_set_pool_info, nvme_add_pool,
                  nvme_get_device, nvme_set_device_status,
                  nvme_add_stream_bond, nvme_get_stream_bond
        :avocado: tags=all,unittest,tiny,regression,vm,smd_ut
        """
        unittest_runner(self, "smd_ut")

    def test_vea_ut(self):
        """
        Test Description: Test vea unittest.
        Use Case: This tests vea's following functions: load, format,
                  query, hint_load, reserve, cancel, tx_publish,
                  free, unload, hint_unload
        :avocado: tags=all,unittest,tiny,regression,vm,vea_ut
        """
        unittest_runner(self, "vea_ut")

    def test_ring_pl_map(self):
        """
        Test Description: Test ring_pl_map unittest.
        Use Case: This tests the ring placement map
        :avocado: tags=all,unittest,tiny,regression,vm,ring_pl_map
        """
        unittest_runner(self, "ring_pl_map")

    def test_jump_pl_map(self):
        """
        Test Description: Test jump_pl_map unittest.
        Use Case: This tests the jump placement map
        :avocado: tags=all,unittest,tiny,regression,vm,jump_pl_map
        """
        unittest_runner(self, "jump_pl_map")

    @skipForTicket("DAOS-1763")
    def test_eq_tests(self):
        """
        Test Description: Test eq_tests unittest.
        Use Case: This tests Daos Event queue
        :avocado: tags=all,unittest,tiny,regression,vm,eq_tests
        """
        unittest_runner(self, "eq_tests")

    def test_vos_tests(self):
        """
        Test Description: Test vos_tests unittest.
        Use Cases: Performs following set of tests - pool_tests,
                   container_tests, io_tests, dtx_tests, aggregate-tests
        :avocado: tags=all,unittest,tiny,regression,vm,vos_tests
        """
        unittest_runner(self, "vos_tests")
