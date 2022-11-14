#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re

from avocado.core.exceptions import TestFail
from ior_test_base import IorTestBase
from telemetry_test_base import TestWithTelemetry


class TelemetryPoolMetrics(IorTestBase, TestWithTelemetry):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Test telemetry pool basic metrics.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TelemetryPoolMetrics object."""
        super().__init__(*args, **kwargs)
        # remove this variable and it's use from the code,
        # once DAOS-8592 is resolved.
        self.threshold_percent = None

    def check_range(self, expected_value, actual_value, threshold_percent):
        """Check if actual value is within expected
           range of expected value
        Args:
          expected_value: Provide expected value to be checked against
          actual_value: Provide the actual value to be checked
          threshold_percent: Threshold percentage for acceptable range

        Return:
          bool: Returns boolean value depending of the check. Returns
                True if the value is within range, else False.

        """
        # Calculate the lower and upper bound values
        upper_bound = (expected_value +
                       (threshold_percent / 100 * expected_value))
        lower_bound = (expected_value -
                       (threshold_percent / 100 * expected_value))
        self.log.debug("Lower Bound: %s", lower_bound)
        self.log.debug("Actual Value: %s", actual_value)
        self.log.debug("Upper Bound: %s", upper_bound)
        # Check if the actual value is within range
        if lower_bound <= actual_value <= upper_bound:
            return True

        return False

    def verify_pool_metrics(self, pool_metrics_list, metrics_data,
                            expected_values):
        """ Verify telemetry pool metrics from metrics_data.

        Args:
            pool_metrics_list (list): list of telemetry pool metrics.
            metrics_data (dict): a dictionary of host keys linked to a
                                 list of pool metric names. Contains
                                 data before and after read/write.
            expected_values (list): List of expected values for different
                                    pool metrics.

        """
        # initializing method params
        final_value = []
        # collecting data to be verified
        for name in sorted(pool_metrics_list):
            self.log.debug("  --telemetry metric: %s", name)
            total = []
            for key in sorted(metrics_data):
                m_data = metrics_data[key]
                # temporary variable to hold total value
                temp = 0
                for host in sorted(m_data[name]):
                    for rank in sorted(m_data[name][host]):
                        for target in sorted(m_data[name][host][rank]):
                            value = m_data[name][host][rank][target]
                            # collecting total value for all targets
                            temp = temp + value
                total.append(temp)
            # subtracting the total value after read/write from total value
            # before read/write
            final_value.append(total[1] - total[0])
            # performing verification now
            if "xferred" in name:
                # check if oclass has any replication and get the replication
                # numeric value, else use 1.
                if True in [char.isdigit() for char in
                            self.ior_cmd.dfs_oclass.value]:
                    replication = re.search(
                        r'\d+', self.ior_cmd.dfs_oclass.value).group()
                else:
                    replication = 1
                # multiply the written amount by replication number
                # to get total expected written amount for only
                # write operation.
                if "update" in name:
                    expected_total_amount_written = (self.ior_cmd.block_size.value *
                                                     int(replication))
                else:
                    expected_total_amount_written = self.ior_cmd.block_size.value
                # get difference between actual written value and expected
                # total written value.
                final_value[-1] = final_value[-1] - expected_total_amount_written
                # check whether the difference obtained in previous step is
                # less thank the chunk size as expected_values[1] carries the
                # chunk size value.
                if final_value[-1] > expected_values[1]:
                    self.fail("Aggregated Value for {} of all the targets "
                              "is greater than expected value of {}"
                              .format(final_value[-1], expected_values[1]))
            else:
                if not self.check_range(expected_values[0],
                                        final_value[-1],
                                        self.threshold_percent):
                    self.fail("Aggregated Value for {} of all the targets "
                              "is out of expected {} percent range"
                              .format(final_value[-1], self.threshold_percent))

    def test_telemetry_pool_metrics(self):
        """JIRA ID: DAOS-8357

            Create files of 500M and 1M with transfer size 1M to verify the
            DAOS engine IO telemetry basic metrics infrastructure.
        Steps:
            Create Pool
            Create Container
            Generate deterministic workload. Using ior to write 512M
            of data with 1M chunk size and 1M transfer size.
            Use telemetry command to get values of 4 parameters
            "engine_pool_ops_fetch", "engine_pool_ops_update",
            "engine_pool_xferred_fetch", "engine_pool_xferred_update"
            for all targets.
            Verify the sum of all parameter metrics matches the workload.
            Do this with RF=0 and RF2(but RP_2GX).
            For RF=0 the sum should be exactly equal to the expected workload
            and for RF=2 (with RP_2GX) sum should be double the size of
            workload for write but same for read.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=telemetry
        :avocado: tags=telemetry_pool_metrics,test_telemetry_pool_metrics
        """
        # test parameters
        self.threshold_percent = self.params.get("threshold_percent",
                                                 "/run/telemetry_metrics/*")
        metric_list = ["engine_pool_ops_fetch",
                       "engine_pool_ops_update",
                       "engine_pool_xferred_fetch",
                       "engine_pool_xferred_update"]
        metrics_data = {}
        # create pool and container
        idx = 0
        self.add_pool(connect=False)
        self.pool.set_property("reclaim", "disabled")
        self.add_container(pool=self.pool)
        # collect first set of pool metric data before read/write
        metrics_data[idx] = self.telemetry.get_pool_metrics(metric_list)
        idx += 1

        # Run ior command.
        try:
            self.update_ior_cmd_with_pool(False)
            self.ior_cmd.dfs_oclass.update(self.container.oclass.value)
            self.run_ior_with_pool(
                timeout=200, create_pool=False, create_cont=False)
        except TestFail:
            self.log.info("#ior command failed!")
        # collect second set of pool metric data after read/write
        metrics_data[idx] = self.telemetry.get_pool_metrics(metric_list)
        # collect data for expected values
        expected_total_objects = (self.ior_cmd.block_size.value /
                                  self.ior_cmd.dfs_chunk.value) + 1
        #     Number of expected total objects, Chunk Size
        check_values = [expected_total_objects, self.ior_cmd.dfs_chunk.value]
        # perform verification check
        self.verify_pool_metrics(metric_list, metrics_data, check_values)
        self.log.info("------Test passed------")
