#!/usr/bin/python
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithoutServers


class HarnessBadInitTest(TestWithoutServers):
    """Test failure in __init__() still produces a test result.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestBadInit object."""
        super().__init__(*args, **kwargs)

        # This should fail with:
        #   AttributeError: 'NoneType' object has no attribute 'endswith'
        self.not_set.endswith(".")

    def test_bad_init(self):
        """Test to verify failure in __init__() still produces a test result.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,harness_bad_init_test,test_bad_init
        """
        self.fail("We should not get here!")
