#!/usr/bin/python3
"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from telemetry_test_base import TestWithTelemetry
from apricot import TestWithServers
from telemetry_utils import TelemetryUtils


class TestWithTelemetryNvme(TestWithTelemetry, TestWithServers):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Test telemetry engine NVMe metrics.

    :avocado: recursive
    """

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
        metrics_data = self.telemetry.get_nvme_metrics(self.server_managers[0])
        self.display_nvme_test_metrics(metrics_data)

        # Get and verify NVMe metrics
        groups = [
                "ENGINE_NVME_HEALTH_METRICS",
                "ENGINE_NVME_CRIT_WARN_METRICS",
                "ENGINE_NVME_TEMP_METRICS",
                "ENGINE_NVME_TEMP_TIME_METRICS",
                "ENGINE_NVME_RELIABILITY_METRICS",
                "ENGINE_NVME_INTEL_VENDOR_METRICS"]

        for group in groups:
            yaml_key = "_".join([group.lower().replace("engine_", ""), "valid"])
            threshold = self.params.get(yaml_key, "/run/*", [None, None])
            test_metrics = getattr(TelemetryUtils, group)
            metrics_data = self.telemetry.get_nvme_metrics(self.server_managers[0], test_metrics)
            desc = " ".join([item.lower() if item != "NVME" else item for item in group.split("_")])
            self.log.info("Verify %s", desc)
            status = self.telemetry.verify_metric_value(metrics_data, threshold[0], threshold[1])
            if not status:
                self.fail("##Telemetry test NVMe metrics verification failed.")

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
