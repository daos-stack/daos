#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import signal
from time import sleep

from apricot import Test


class TestSignalHandler(Test):
    # pylint: disable=too-few-public-methods
    """Test signals sent by avocado.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestSignalHandler object."""
        super(TestSignalHandler, self).__init__(*args, **kwargs)
        self.sigterm_sleep = 0

    def test_sigterm(self):
        """Test ignoring the SIGTERM signal.

        This test verifies avocado behavior when it detects a test that times
        out and may or may not block a SIGTERM.  The 'timeout_test' variants of
        this test are expected to be INTERRUPTED.

        This test can be run in any CI stage: vm, small, medium, large

        :avocado: tags=test_harness,test_signal_handler,test_sigterm
        """
        def sigterm_handler(signal_number, frame):
            # pylint: disable=unused-argument
            self.log.info("=== Caught a %s signal ===", signal_number)
            self.log.info("=== Sleeping for %s seconds ===", self.sigterm_sleep)
            sleep(self.sigterm_sleep)

        # Get the number of seconds to sleep after catching a SIGTERM signal and
        # the number of seconds to sleep in the test case - used to trigger the
        # avocado test timeout
        self.sigterm_sleep = self.params.get("sigterm_sleep")
        test_sleep = self.params.get("test_sleep")

        # Add the SIGTERM handler
        signal.signal(signal.SIGTERM, sigterm_handler)

        # Sleep
        self.log.info("sleeping for %s seconds", test_sleep)
        sleep(test_sleep)
        self.log.info("Test complete!")
