#!/usr/bin/python
"""
  (C) Copyright 2021 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

'''
Skip-list harness testing
'''

import os
from apricot import Test

class TestHarnessSkipsBase(Test):
    """ Base class for test classes below """

    def setUp(self):
        # Use our own CI-skip-list-master to test to run these tests
        self.cancel_file = os.path.join(os.sep, 'tmp', 'skip_list')
        with open(self.cancel_file, 'w') as skip_handle:
            skip_handle.write(
                '''[['DAOS-0000', 'test_method_name', 'test_case_1']]
[['DAOS-0000', 'test_method_name', 'test_case_2']]|b4a912f
[['DAOS-0000', 'test_method_name', 'test_case_3']]|abcd123
[['DAOS-9999', 'test_method_name', 'test_case_4']]
[['DAOS-9999', 'test_method_name', 'test_case_5']]|b4a912f
[['DAOS-9999', 'test_method_name', 'test_case_6']]|abcd123''')
        self.cancel_file = self.cancel_file
        super(TestHarnessSkipsBase, self).setUp()

class TestHarnessSkipsSkipped(TestHarnessSkipsBase):
    """
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        super(TestHarnessSkipsSkipped, self).__init__(*args, **kwargs)
        self.cancelled = False

    def cancel(self, msg):
        """ Override Avocado Test.cancel() """
        self.log.info("Test correctly called cancel(%s)" % msg)
        self.cancelled = True

    def test_case_1(self):
        '''
        w/o a fix in this commit w/o a committed fix
        :avocado: tags=all
        :avocado: tags=harness, test_skips
        :avocado: tags=test_case_1
        '''
        if not self.cancelled:
            self.fail("This test was not skipped as it should have been")

    def test_case_3(self):
        '''
        w/o a fix in this commit w/ a committed fix not in this code base
        :avocado: tags=all
        :avocado: tags=harness, test_skips
        :avocado: tags=test_case_3
        '''
        if not self.cancelled:
            self.fail("This test was not skipped as it should have been")

class TestHarnessSkipsRun(TestHarnessSkipsBase):
    """
    :avocado: recursive
    """

    def cancel(self, _msg):
        """ override Test.cancel() """
        self.fail('This test should not be skipped')

    def test_case_2(self):
        '''
        w/o a fix in this commit w/ a committed fix in this code base
        :avocado: tags=all
        :avocado: tags=harness, test_skips
        :avocado: tags=test_case_2
        '''

    def test_case_4(self):
        '''
        w/ a fix in this commit w/o a committed fix
        :avocado: tags=all
        :avocado: tags=harness, test_skips
        :avocado: tags=test_case_4
        '''

    def test_case_5(self):
        '''
        w/ a fix in this commit w/ a committed fix in this code base
        :avocado: tags=all
        :avocado: tags=harness, test_skips
        :avocado: tags=test_case_5
        '''

    def test_case_6(self):
        '''
        w/ a fix in this commit w/ a committed fix not in this code base
        :avocado: tags=all
        :avocado: tags=harness, test_skips
        :avocado: tags=test_case_6
        '''
