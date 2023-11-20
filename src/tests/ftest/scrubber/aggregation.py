"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from scrubber_test_base import TestWithScrubber
from telemetry_test_base import TestWithTelemetry


class TestScrubberEvictWithAggregation(TestWithScrubber, TestWithTelemetry):
    # pylint: disable=too-many-ancestors
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

        :avocado: tags=all,manual
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
        telemetry_string = "engine_pool_vos_aggregation_obj_scanned"
        initial_agg_metrics = self.telemetry.get_metrics(telemetry_string)
        # Enable the aggregation on the pool.
        self.pool.set_property("reclaim", "time")
        # Now enable the scrubber on the pool.
        self.pool.set_prop(properties="scrub:timed,scrub-freq:1,scrub-thresh:3")
        initial_metrics = self.scrubber.get_scrub_corrupt_metrics()
        self.run_ior_and_check_scruber_status(pool=self.pool, cont=self.container)
        # We want both aggregation and scrubber tasks
        # to run in parallel during this time.
        start_time = 0
        finish_time = 0
        start_time = time.time()
        while int(finish_time - start_time) < 120:
            final_agg_metrics = self.telemetry.get_metrics(telemetry_string)
            status = self.verify_scrubber_metrics_value(initial_agg_metrics,
                                                        final_agg_metrics)
            # aggregation counters are changing (which means aggregation has started)
            if status is True:
                break
            # Wait for 10 seconds before querying the metrics value.
            time.sleep(10)
            finish_time = time.time()
        self.pool.query()
        final_metrics = self.scrubber.get_scrub_corrupt_metrics()
        status = self.verify_scrubber_metrics_value(initial_metrics, final_metrics)
        # Compare the initial scrubber corrupt metrics with the final values.
        # If they differ, the test passed. If not, the test failed.
        if status is False:
            self.log.info("------Scrubber Aggregation Test Failed-----")
            self.fail("-Test Failed: Scrubber corrupt metrics values doesn't change-")
        self.log.info("------Scrubber Aggregation Passed------")
