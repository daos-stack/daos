"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from scrubber_test_base import TestWithScrubber


class TestWithMdTestScrubberFault(TestWithScrubber):
    # pylint: disable=too-many-nested-blocks,too-many-ancestors
    """Inject Checksum Fault with scrubber enabled for MdTest run.

    :avocado: recursive
    """
    def test_scrubber_mdtest_csum_fault(self):
        """JIRA ID: DAOS-5938

            1. Create checksum faults and see
            whether scrubber finds them.
            2. Run mdtest application as part of
            the testing.
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=scrubber,faults
        :avocado: tags=TestWithMdTestScrubberFault,test_scrubber_mdtest_csum_fault

        """
        pool_prop = self.params.get("properties", '/run/pool/*')
        cont_prop = self.params.get("properties", '/run/container/*')
        mdtest_params = self.params.get("mdtest_params", "/run/mdtest/*")
        initial_metrics = {}
        final_metrics = {}
        # Create a pool and container
        self.create_pool_cont_with_scrubber(pool_prop=pool_prop, cont_prop=cont_prop)
        initial_metrics = self.scrubber.get_scrub_corrupt_metrics()
        self.run_mdtest_and_check_scruber_status(pool=self.pool, cont=self.container,
                                                 mdtest_params=mdtest_params)
        start_time = 0
        finish_time = 0
        poll_status = False
        start_time = time.time()
        while int(finish_time - start_time) < 60 and poll_status is False:
            final_metrics = self.scrubber.get_scrub_corrupt_metrics()
            status = self.verify_scrubber_metrics_value(initial_metrics, final_metrics)
            if status is False:
                self.log.info("------Test Failed-----")
                self.log.info("---No metrics value change----")
            else:
                poll_status = True
            finish_time = time.time()
        if poll_status is True:
            self.log.info("------Test passed------")
        else:
            self.fail("------Test Failed-----")
