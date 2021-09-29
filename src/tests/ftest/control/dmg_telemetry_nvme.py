#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from telemetry_test_base import TestWithTelemetry
from apricot import TestWithServers
from telemetry_utils import TelemetryUtils

class TestWithTelemetryNvme(TestWithTelemetry,TestWithServers):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Test telemetry engine NVMe metrics.

    :avocado: recursive
    """

    def verify_nvme_test_metrics(self, metrics_data, threshold=None):
        """ Verify telemetry NVMe metrics from metrics_data.

        Args:
            nvme_test_metrics (list): list of telemetry NVMe metrics.
            metrics_data (dict): a dictionary of host keys linked to a
                                 list of NVMe metric names.
            threshold (int): test NVMe metrics threshold, None will just
                             check for valid number >= 0 instead of checking
                             valid threshold range.
        """
        self.log.info("Verify threshold of metrics")
        status = True
        for name in sorted(metrics_data):
            self.log.info("    --telemetry metric: %s", name)
            self.log.info("    %-12s %-4s %s", "Host", "Rank", "Value")
            for host in sorted(metrics_data[name]):
                for rank in sorted(metrics_data[name][host]):
                    value = metrics_data[name][host][rank]
                    invalid = "Metric value in range"
                    #Verify metrics are within allowable threshold
                    if threshold is None:
                        if value < 0:
                            status = False
                            invalid = "Metric value less than 0"
                    elif (value < threshold[0] or value > threshold[1]):
                        status = False
                        invalid = "Metric value out of valid range"

                    self.log.info("    %-12s %-4s %s %s",
                                  host, rank, value, invalid)

        if not status:
            self.fail("##Telemetry test NVMe metrics verification failed.")

    def display_nvme_test_metrics(self, metrics_data):
        """ Display NVMe metrics_data.

        Args:
            metrics_data (dict): a dictionary of host keys linked to a
                                 list of NVMe metric names.
        """
        for key in sorted(metrics_data):
            self.log.info(
                    "\n  %12s: %s",
                    "Initial " if key == 0 else "Test Loop {}".format(key),
                    metrics_data[key])

    def test_nvme_telemetry_metrics(self):
        """JIRA ID: DAOS-7833

            Verify the telemetry engine NVMe metrics.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,small
        :avocado: tags=control,telemetry,nvme
        :avocado: tags=test_nvme_telemetry_metrics
        """
        metrics_data = {}
        metrics_data = self.telemetry.get_nvme_metrics(self.server_managers[0])
        self.display_nvme_test_metrics(metrics_data)

        # Get and verfiy NVMe health metrics
        test_metrics = TelemetryUtils.ENGINE_NVME_HEALTH_METRICS
        metrics_data = self.telemetry.get_nvme_metrics(self.server_managers[0],
                                                       test_metrics)
        self.log.info("Verify NVMe health metrics")
        self.verify_nvme_test_metrics(metrics_data)

        # Get and verify NVMe critical warning metrics
        threshold = self.params.get("nvme_test_crit_warn_metrics_valid", "/run/*")
        test_metrics = TelemetryUtils.ENGINE_NVME_CRIT_WARN_METRICS
        metrics_data = self.telemetry.get_nvme_metrics(self.server_managers[0],
                                                       test_metrics)
        self.log.info("Verify NVMe critical warning metrics")
        self.verify_nvme_test_metrics(metrics_data, threshold)

        # Get and verify NVMe temperature metrics
        threshold = self.params.get("nvme_test_temp_metrics_valid", "/run/*")
        test_metrics = TelemetryUtils.ENGINE_NVME_TEMP_METRICS
        metrics_data = self.telemetry.get_nvme_metrics(self.server_managers[0],
                                                       test_metrics)
        self.log.info("Verify NVMe temperature metrics")
        self.verify_nvme_test_metrics(metrics_data, threshold)
        threshold = self.params.get("nvme_test_temp_time_metrics_valid", "/run/*")
        test_metrics = TelemetryUtils.ENGINE_NVME_TEMP_TIME_METRICS
        metrics_data = self.telemetry.get_nvme_metrics(self.server_managers[0],
                                                       test_metrics)
        self.log.info("Verify NVMe temperature time metrics")
        self.verify_nvme_test_metrics(metrics_data, threshold)

        # Get and verify NVMe reliability metrics
        threshold = self.params.get("nvme_test_reliability_metrics_valid", "/run/*")
        test_metrics = TelemetryUtils.ENGINE_NVME_RELIABILITY_METRICS
        metrics_data = self.telemetry.get_nvme_metrics(self.server_managers[0],
                                                       test_metrics)
        self.log.info("Verify NVMe reliability metrics")
        self.verify_nvme_test_metrics(metrics_data, threshold)

        # Get and verify NVMe Intel vendor metrics
        test_metrics = TelemetryUtils.ENGINE_NVME_INTEL_VENDOR_METRICS
        metrics_data = self.telemetry.get_nvme_metrics(self.server_managers[0],
                                                       test_metrics)
        self.log.info("Verify NVMe Intel vendor metrics")
        self.verify_nvme_test_metrics(metrics_data)

        self.log.info("------Test passed------")

    def test_telemetry_list_nvme(self):
        """JIRA ID: DAOS-7667 / SRS-324.

        Test Description:
            Verify the dmg telemetry list command.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,small
        :avocado: tags=control,telemetry,nvme
        :avocado: tags=test_with_telemetry_nvme,test_telemetry_list_nvme
        """
        self.verify_telemetry_list()
