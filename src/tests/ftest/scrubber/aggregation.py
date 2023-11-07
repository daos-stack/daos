"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from scrubber_test_base import TestWithScrubber


class TestScrubberEvictWithAggregation(TestWithScrubber):
    # pylint: disable=too-many-nested-blocks
    """Inject Checksum Fault with scrubber enabled
    and scrubber threshold set to a certain value.
    Aggregation is run on the background.

    :avocado: recursive
    """
    def test_target_eviction_during_aggregation(self):
        """JIRA ID: DAOS-7333

        1. Start the Aggregation task.
        2. Create checksum faults above scrubber threshold
        and see whether SSD auto eviction works as expected.
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=scrubber,faults
        :avocado: tags=TestScrubberEvictWithAggregation,test_target_eviction_during_aggregation
        """
        initial_metrics = {}
        final_metrics = {}
        self.add_pool()
        # Disable the aggregation on the pool.
        self.pool.set_property("reclaim", "disabled")
        self.add_container(self.pool)
        # Pool and Containers are already created. Just run the IOR.
        self.run_ior_with_pool(create_cont=False)
        # Enable the aggregation on the pool.
        self.pool.set_property("reclaim", "time")
        self.pool.set_property("scrub", "timed")
        self.pool.set_property("scrub-freq", "1")
        self.pool.set_property("scrub-thresh", "3")
        initial_metrics = self.scrubber.get_scrub_corrupt_metrics()
        self.run_ior_and_check_scruber_status(pool=self.pool, cont=self.container)
        # Wait for two minutes for aggregation run and scrubber to take to evict target
        # after corruption threshold reached. We want both aggregation and scrubber tasks
        # to run in parallel during this time.
        time.sleep(120)
        self.dmg_cmd.pool_query(self.pool.identifier)
        final_metrics = self.scrubber.get_scrub_corrupt_metrics()
        status = self.verify_scrubber_metrics_value(initial_metrics, final_metrics)
        if status is False:
            self.log.info("------Test Failed-----")
            self.log.info("---No metrics value change----")
            self.fail("------Test Failed-----")
        self.log.info("------Test passed------")
