#!/usr/bin/python

'''
Very basic harness testing
'''

from apricot import Test

class HarnessBasicTest(Test):
    """
    :avocado: recursive
    """

    def test_always_passes(self):
        """
        :avocado: tags=all
        :avocado: tags=harness
        :avocado: tags=always_passes
        """

        self.log.info("NOOP test to do nothing but run successfully")