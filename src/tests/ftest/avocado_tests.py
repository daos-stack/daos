#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time
from apricot import Test


class ApricotTests(Test):
    """Test class to do Apricot testing.

    Test Class Description:
        This class contains tests to test Apricot.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super(ApricotTests, self).__init__(*args, **kwargs)

        print("__init__()")
        self.timeout = 9


    def setUp(self):
        """Set up."""
        print("setUp() start")
        super(ApricotTests, self).setUp()

    def tearDown(self):
        """Tear down."""
        print("tearDown() start")
        super(ApricotTests, self).tearDown()

        time.sleep(5)
        print("tearDown() ended after 5s sleep")


    def test_junit_stdio(self):
        """Test full Stdout in Jenkins JUnit display

        :avocado: tags=avocado_tests,avocado_junit_stdout
        """
        with open('large_stdout.txt', 'r') as inp:
            print(inp.read())
        self.fail()


    def test_teardown_timeout_timed_out(self):
        """Test the PoC tearDown() timeout patch

        :avocado: tags=avocado_tests,avocado_test_teardown_timeout
        :avocado: tags=avocado_test_teardown_timeout_timed_out
        """
        self.log.debug("Sleeping for 10 seconds")
        time.sleep(10)


    def test_teardown_timeout(self):
        """Test the PoC tearDown() timeout patch

        :avocado: tags=avocado_tests,avocado_test_teardown_timeout
        """
        self.log.debug("Sleeping for 1 second")
        time.sleep(1)
