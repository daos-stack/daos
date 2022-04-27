#!/usr/bin/python3
'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
from avocado.utils import process
from apricot import TestWithoutServers


def unittest_runner(self, unit_testname):
    """Run a unittest.

    Unit tests needs to be run on local machine in case of server start required
    For other unit tests, which does not required to start server,it needs to be
    run on server where /mnt/daos mounted.

    Args:
        unit_testname (str): unittest name.
    """
    name = self.params.get("testname", '/run/UnitTest/{0}/'
                           .format(unit_testname))
    server = self.params.get("test_servers", "/run/hosts/*")
    test_exe = os.path.join(self.bin, name)

    cmd = ("/usr/bin/ssh {} {}".format(server[0], test_exe))

    return_code = process.system(cmd, ignore_status=True,
                                 allow_output_check="both")

    if return_code != 0:
        self.fail("{0} unittest failed with return code={1}.\n"
                  .format(unit_testname, return_code))


class UnitTestWithoutServers(TestWithoutServers):
    """
    Test Class Description: Avocado Unit Test class for tests which don't
                            need servers.
    :avocado: recursive
    """

    def test_agent_tests(self):
        """
        Test Description: Test daos agent unittest.
        Use Cases: daos_agent tests for connection.
        :avocado: tags=all,unittest,tiny,full_regression,agent_tests
        """
        unittest_runner(self, "agent_tests")

    def test_job_tests(self):
        """
        Test Description: Test daos job unittest.
        Use Cases: daos_job tests for job environment variables.
        :avocado: tags=all,unittest,tiny,full_regression,job_tests
        """
        unittest_runner(self, "job_tests")
