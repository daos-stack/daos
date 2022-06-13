#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from scrubber_test_base import TestWithScrubber


class TestWithScrubberTargetEviction(TestWithScrubber):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Inject Checksum Fault with scrubber enabled
    and scrubber threshold set to a certain value.

    :avocado: recursive
    """
    def test_scrubber_ssd_auto_eviction(self):
        """JIRA ID: DAOS-7300

        1. Create checksum faults above scrubber threshold
        and see whether SSD auto eviction works as expected.
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=scrubber,faults
        :avocado: tags=test_scrubber_target_auto_eviction

        """
        pool_prop = self.params.get("properties", '/run/pool/*')
        cont_prop = self.params.get("properties", '/run/container/*')
        initial_metrics = {}
        final_metrics = {}
        self.create_pool_cont_with_scrubber(pool_prop=pool_prop, cont_prop=cont_prop)
        self.dmg_cmd.pool_query(self.pool.uuid)
        initial_metrics = self.scrubber.get_scrub_corrupt_metrics()
        self.run_ior_and_check_scruber_status(pool=self.pool, cont=self.container)
        # Wait for a minute for the scrubber to take action and evict target
        # after corruption threshold reached.
        time.sleep(60)
        self.dmg_cmd.pool_query(self.pool.uuid)
        final_metrics = self.scrubber.get_scrub_corrupt_metrics()
        status = self.verify_scrubber_metrics_value(initial_metrics, final_metrics)
        if status is False:
            self.log.info("------Test Failed-----")
            self.log.info("---No metrics value change----")
            self.fail("------Test Failed-----")
        self.log.info("------Test passed------")
