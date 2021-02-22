#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from __future__ import print_function

import sys
import subprocess

from apricot       import TestWithoutServers

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class CartNoPmixOneNodeTest(TestWithoutServers):
    """
    Runs basic CaRT no_pmix tests

    :avocado: recursive
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        super(CartNoPmixOneNodeTest, self).setUp()
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def tearDown(self):
        """ Tear down """
        self.report_timeout()
        self._teardown_errors.extend(self.utils.cleanup_processes())
        super(CartNoPmixOneNodeTest, self).tearDown()

    def test_cart_no_pmix(self):
        """
        Test CaRT NoPmix

        :avocado: tags=all,cart,pr,daily_regression,no_pmix,one_node
        """

        cmd = self.params.get("tst_bin", '/run/tests/*/')

        self.utils.print("\nTest cmd : %s\n" % cmd)

        p = subprocess.Popen([cmd], stdout=subprocess.PIPE)

        rc = self.utils.wait_process(p, 30)
        if rc != 0:
            self.utils.print("Error waiting for process.")
            self.utils.print("returning {}".format(rc))
            self.fail("Test failed.\n")

        self.utils.print("Finished waiting for {}".format(p))

