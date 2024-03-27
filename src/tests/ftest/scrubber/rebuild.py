"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from scrubber_test_base import TestWithScrubber


class TestScrubberEvictWithRebuild(TestWithScrubber):
    """Inject Checksum Fault with scrubber enabled
    and scrubber threshold set to a certain value.
    Rebuild is run on the background.

    :avocado: recursive
    """
    def test_target_eviction_during_rebuild(self):
        """JIRA ID: DAOS-7333

        - Perform target eviction while rebuild is happening
        in the background.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=scrubber,faults
        :avocado: tags=TestScrubberEvictWithRebuild,test_target_eviction_during_rebuild
        """
        pool_prop = self.params.get("properties", '/run/pool/*')
        cont_prop = self.params.get("properties", '/run/container/*')
        initial_metrics = {}
        final_metrics = {}
        self.create_pool_cont_with_scrubber(pool_prop=pool_prop, cont_prop=cont_prop)
        self.pool.query()
        initial_metrics = self.scrubber.get_scrub_corrupt_metrics()
        self.run_ior_and_check_scruber_status(pool=self.pool, cont=self.container)
        # Exclude Rank 5 to start the rebuild operation.
        self.pool.exclude("5")
        # Wait for a minute for the scrubber to take action and evict target
        # after corruption threshold reached.
        time.sleep(60)
        self.pool.query()
        final_metrics = self.scrubber.get_scrub_corrupt_metrics()
        status = self.verify_scrubber_metrics_value(initial_metrics, final_metrics)
        # Compare the initial scrubber corrupt metrics with the final values.
        # If they differ, the test passed. If not, the test failed
        if status is False:
            self.log.info("------Scrubber Rebuild Test Failed-----")
            self.fail("-Test Failed: Scrubber corrupt metrics values doesn't change-")
        self.log.info("------Scrubber Rebuild Test Passed------")
