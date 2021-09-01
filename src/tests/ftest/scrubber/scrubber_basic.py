#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from scrubber_test_base import TestWithScrubber

class TestWithScrubberBasic(TestWithScrubber):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Basic Scrubber Test

    :avocado: recursive
    """

    def test_scrubber_basic(self):
        """JIRA ID: DAOS-8099

            1. Enable scrubber on a test pool and gather
               scrubber statistics
            2. Enable scrubber, run IOR and gather
               scrubber statistics
            3. Disable checksum on a container and run
               IOR. Gather scrubber statistics.
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=scrubber
        :avocado: tags=test_scrubber_basic

        """
        self.create_pool_cont_with_scrubber()
        self.run_ior_and_check_scruber_status()
        self.log.info("------Test passed------")
