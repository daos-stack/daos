"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time

from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics


class DaosAggregationThrottling(IorTestBase):
    """Test class Description:

       Verify ior performance during aggregation throttling

    :avocado: recursive
    """

    def test_aggregation_throttling(self):
        """Jira ID: DAOS-3749

        Test Description:
            Verify the ior throttling during aggregation
            in the background is affecting the ior
            performance only by +/- 30%
        Use case:
            Create a pool and container
            Disable the aggregation
            Run ior with same file option
            Capture the initial ior performance.
            Run ior the second time with same file option, so the
            logical partitioning can be overwritten.
            Enable the aggregation and wait for 90 seconds
            Now as the aggregation is running in the background, run
            ior again so both aggregation and ior runs in parallel
            Capture the ior performance now and verify that it is
            +/- 30% of the initial performance.
            Also, verify the aggregation reclaimed the space used by
            second ior.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=aggregation,daosio,ior
        :avocado: tags=DaosAggregationThrottling,test_aggregation_throttling
        """
        # Create pool and container
        self.update_ior_cmd_with_pool()

        # Disable the aggregation
        self.pool.set_property("reclaim", "disabled")

        # Run ior with -k option to retain the file and not delete it
        out = self.run_ior_with_pool()
        metric_before_aggregate = IorCommand.get_ior_metrics(out)

        # Run ior the second time on the same pool and container, so another
        # copy of the file is inserted in DAOS.
        out = self.run_ior_with_pool(create_pool=False)

        # wait 90 seconds for files to get old enough for aggregation
        self.log.info("Waiting for 90 seconds for aggregation to start")
        time.sleep(90)
        # Enable the aggregation
        self.pool.set_property("reclaim", "time")

        # Run ior the third time while the aggregation of first two runs
        # are running in the background.
        out = self.run_ior_with_pool(create_pool=False)
        metric_after_aggregate = IorCommand.get_ior_metrics(out)

        # When DAOS-5057 is fixed, adjust the percentage. For now,
        # keep it at 30 %
        expected_perf_diff = 30.0

        self.verify_performance(metric_before_aggregate,
                                metric_after_aggregate,
                                0,  # write_perf
                                expected_perf_diff)  # 30% perf difference

        self.verify_performance(metric_before_aggregate,
                                metric_after_aggregate,
                                1,  # read_perf
                                expected_perf_diff)

    def verify_performance(self, before_metric, after_metric, read_write_idx, expected_perf_diff):
        """Verify the after_metric read/write performances are within
           +/- expected_perf_diff % of before_metric_performance.

        Args:
           before_metric (tuple): ior metrics before aggregation, from get_ior_metrics
           after_metric (tuple): ior metrics in concurrent with aggregation, from get_ior_metrics
           read_write_idx (int): Read (1) or Write (0) index in the metrics
           expected_perf_diff (float): Expected performance difference between
                                       before_metric and after_metric.
        """
        self.log.info("\n\n %s Performance", "Read" if read_write_idx else "Write")

        max_prev = float(before_metric[read_write_idx][IorMetrics.MAX_MIB])
        max_curr = float(after_metric[read_write_idx][IorMetrics.MAX_MIB])
        self.log.info("max_prev = %s, max_curr = %s", max_prev, max_curr)

        min_prev = float(before_metric[read_write_idx][IorMetrics.MIN_MIB])
        min_curr = float(after_metric[read_write_idx][IorMetrics.MIN_MIB])
        self.log.info("min_prev = %s, min_curr = %s", min_prev, min_curr)

        mean_prev = float(before_metric[read_write_idx][IorMetrics.MEAN_MIB])
        mean_curr = float(after_metric[read_write_idx][IorMetrics.MEAN_MIB])
        self.log.info("mean_prev = %s, mean_curr = %s", mean_prev, mean_curr)

        max_perf_diff = (abs(max_prev - max_curr) / max_prev) * 100
        min_perf_diff = (abs(min_prev - min_curr) / min_prev) * 100
        mean_perf_diff = (abs(mean_prev - mean_curr) / mean_prev) * 100

        self.log.info("Max perf diff %s < %s", max_perf_diff, expected_perf_diff)
        self.assertTrue(max_perf_diff < expected_perf_diff)
        self.log.info("Min perf diff %s < %s", min_perf_diff, expected_perf_diff)
        self.assertTrue(min_perf_diff < expected_perf_diff)
        self.log.info("Mean perf diff %s < %s", mean_perf_diff, expected_perf_diff)
        self.assertTrue(mean_perf_diff < expected_perf_diff)
