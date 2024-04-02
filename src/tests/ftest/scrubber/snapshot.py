"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from scrubber_test_base import TestWithScrubber


class TestScrubberEvictWithSnapshot(TestWithScrubber):
    """Inject Checksum Fault with scrubber enabled
    and scrubber threshold set to a certain value.
    Snapshot taken while checksum faults are injected.

    :avocado: recursive
    """
    def test_target_eviction_during_snapshot(self):
        """JIRA ID: DAOS-7333

        - Take a container snapshot while the csum faults
        are generated and target is evicted eventually.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=scrubber,faults
        :avocado: tags=TestScrubberEvictWithSnapshot,test_target_eviction_during_snapshot
        """
        pool_prop = self.params.get("properties", '/run/pool/*')
        cont_prop = self.params.get("properties", '/run/container/*')
        initial_metrics = {}
        final_metrics = {}
        self.create_pool_cont_with_scrubber(pool_prop=pool_prop, cont_prop=cont_prop)
        self.pool.query()
        initial_metrics = self.scrubber.get_scrub_corrupt_metrics()
        self.run_ior_and_check_scruber_status(pool=self.pool, cont=self.container)
        # Wait for a minute for the scrubber to take action and evict target
        # after corruption threshold reached.
        # Take a snap-shot after 15 seconds while the csum faults are injected.
        time.sleep(15)
        self.container.create_snap()
        time.sleep(45)
        self.pool.query()
        final_metrics = self.scrubber.get_scrub_corrupt_metrics()
        status = self.verify_scrubber_metrics_value(initial_metrics, final_metrics)
        # Compare the initial scrubber corrupt metrics with the final values.
        # If they differ, the test passed. If not, the test failed
        if status is False:
            self.log.info("------Scrubber Snapshot Test Failed-----")
            self.fail("-Test Failed: Scrubber corrupt metrics values doesn't change-")
        self.log.info("------Scrubber Snapshot Test Passed------")
