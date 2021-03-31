#!/usr/bin/python
"""
  (C) Copyright 2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import Test


class HarnessBasicTest(Test):
    """Very basic harness test cases.

    :avocado: recursive
    """

    def test_always_passes(self):
        """Simple test of apricot test code.

        :avocado: tags=all
        :avocado: tags=harness,harness_basic_test,test_always_passes
        :avocado: tags=always_passes
        """
        self.log.info("NOOP test to do nothing but run successfully")

    def test_always_passes_hw(self):
        """Simple test of apricot test code.

        :avocado: tags=all
        :avocado: tags=hw,large,medium,ib2,small
        :avocado: tags=harness,harness_basic_test,test_always_passes_hw
        :avocado: tags=always_passes
        """
        self.test_always_passes()
