#!/usr/bin/python
"""
  (C) Copyright 2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from time import sleep

from apricot import Test


class HarnessTimeoutTest(Test):
    """Advanced harness test cases.

    :avocado: recursive
    """

    def tearDown(self):
        """Tear down after each test case."""
        super().tearDown()

        # Sleep almost the full 60 seconds to verify that this method is not
        # interrupted
        self.log.info("Verifying that timeout is given 60 seconds to execute")
        self.log.info("Sleeping for 59 seconds")
        sleep(59)

    def test_timeout(self):
        """Test to verify time allotted for tearDown after test timeout.

        This test will send a signal 6 to a random daos_engine process so
        that it will create a core file, allowing the core file collection code
        in launch.py to be tested.

        This test can be run in any CI stage: vm, small, medium, large

        :avocado: tags=all
        :avocado: tags=harness,harness_timeout_test,test_timeout
        """
        sleep_timeout = self.timeout + 1
        self.log.info(
            "Timing out the test method with a %s second sleep", sleep_timeout)

        # Force a timeout for the execution of this method
        self.log.info("*** TEST IS EXPECTED TO BE INTERRUPTED ***")
        sleep(sleep_timeout)

        # We should not get this far
        self.fail("Test did not timeout!")

    def test_timeout_hw(self):
        """Test to verify time allotted for tearDown after test timeout.

        This test will send a signal 6 to a random daos_engine process so
        that it will create a core file, allowing the core file collection code
        in launch.py to be tested.

        This test can be run in any CI stage: vm, small, medium, large

        :avocado: tags=all
        :avocado: tags=hw,small,medium,ib2,large
        :avocado: tags=harness,harness_timeout_test,test_timeout_hw
        """
        self.test_timeout()
