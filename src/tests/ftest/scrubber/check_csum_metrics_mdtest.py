"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from mdtest_test_base import MdtestBase
from scrubber_test_base import TestWithScrubber


class CheckCsumMetricsMdtest(TestWithScrubber, MdtestBase):
    # pylint: disable=too-many-ancestors
    """Check the checksum metrics with scrubber enabled during MdTest run.

    :avocado: recursive
    """
    def test_scrubber_csum_metrics_with_mdtest(self):
        """JIRA ID: DAOS-5938

            1. Start the scrubber.
            2. Get the Checksum metrics
            3. Run mdtest application.
            4. Gather the scrubber metrics related to checksum
            and compare it with initial values.
        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=scrubber,mdtest
        :avocado: tags=CheckCsumMetricsMdtest,test_scrubber_csum_metrics_with_mdtest

        """
        pool_prop = self.params.get("properties", '/run/pool/*')
        cont_prop = self.params.get("properties", '/run/container/*')
        initial_metrics = {}
        final_metrics = {}
        # Create a pool and container
        self.create_pool_cont_with_scrubber(pool_prop=pool_prop, cont_prop=cont_prop)
        initial_metrics = self.scrubber.get_csum_total_metrics()
        self.execute_mdtest()
        start_time = 0
        finish_time = 0
        poll_status = False
        start_time = time.time()
        while int(finish_time - start_time) < 60 and poll_status is False:
            final_metrics = self.scrubber.get_csum_total_metrics()
            status = self.verify_scrubber_metrics_value(initial_metrics, final_metrics)
            if status is False:
                self.log.info("---Scrubber metrics value doesn't change---")
            else:
                poll_status = True
            finish_time = time.time()
        if poll_status is True:
            self.log.info("--Scrubber testing with mdtest: Test passed--")
        else:
            self.fail("--Scrubber metrics value doesn't change (mdtest) : Test Failed--")
